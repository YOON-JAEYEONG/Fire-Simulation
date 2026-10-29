#include "Simulation/YUFSReplayArchive.h"
#include "HAL/FileManager.h"
#include "Misc/Compression.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace
{
constexpr uint32 Magic = 0x5952504c; // YRPL
// v2 adds posture, frame clock/population and door/extinguisher state. v1 still loads.
constexpr uint32 Version = 2;
constexpr uint32 MinVersion = 1;
constexpr int32 MaxBytes = 512 * 1024 * 1024;

FString RecordingPath(const FString& Id)
{
	FGuid Guid;
	if (!FGuid::ParseExact(Id, EGuidFormats::Digits, Guid)) return FString();
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Replays"), Guid.ToString(EGuidFormats::Digits) + TEXT(".yufsreplay"));
}

// Bounded UTF-8 strings keep a damaged archive from allocating arbitrary lengths.
void TextField(FArchive& Ar, FString& Text)
{
	if (Ar.IsSaving())
	{
		FTCHARToUTF8 Utf8(*Text);
		int32 Count = Utf8.Length();
		Ar << Count;
		Ar.Serialize(const_cast<ANSICHAR*>(Utf8.Get()), Count);
	}
	else
	{
		int32 Count = 0;
		Ar << Count;
		if (Count < 0 || Count > 16384 || Count > Ar.TotalSize() - Ar.Tell()) { Ar.SetError(); return; }
		TArray<ANSICHAR> Buffer;
		Buffer.SetNumZeroed(Count + 1);
		Ar.Serialize(Buffer.GetData(), Count);
		Text = UTF8_TO_TCHAR(Buffer.GetData());
	}
}

template<typename T> void EnumField(FArchive& Ar, T& Value)
{
	uint8 Byte = static_cast<uint8>(Value);
	Ar << Byte;
	if (Ar.IsLoading()) Value = static_cast<T>(Byte);
}

void NameField(FArchive& Ar, FName& Value)
{
	FString Text = Value.ToString();
	TextField(Ar, Text);
	if (Ar.IsLoading() && !Ar.IsError()) Value = FName(*Text);
}

void Snapshot(FArchive& Ar, FYUFSTimelineNPCSnapshot& S, uint32 FileVersion)
{
	FString Name = S.NPCId.ToString(), Asset = S.AnimationAssetPath.ToString();
	TextField(Ar, Name);
	TextField(Ar, Asset);
	if (Ar.IsError()) return;
	if (Ar.IsLoading()) { S.NPCId = FName(*Name); S.AnimationAssetPath = FSoftObjectPath(Asset); }
	Ar << S.StableNPCId << S.Location << S.Rotation << S.Velocity;
	EnumField(Ar, S.DisplayAction);
	Ar << S.AnimationPositionSeconds << S.AnimationPlayRate << S.AnimationLengthSeconds;
	Ar << S.bAnimationLooping << S.bHasAnimationState;
	EnumField(Ar, S.BehaviorState); EnumField(Ar, S.CurrentAction);
	EnumField(Ar, S.Intent); EnumField(Ar, S.ActionTask);
	Ar << S.CommitProbability << S.PreActionCompletedCount << S.PreActionTargetCount;
	Ar << S.RiskPerception << S.SmokeExposure << S.bVisible << S.bEvacuated << S.bIncapacitated;
	if (S.Location.ContainsNaN() || S.Rotation.ContainsNaN() || S.Velocity.ContainsNaN()
		|| !FMath::IsFinite(S.AnimationPositionSeconds) || !FMath::IsFinite(S.AnimationPlayRate)) Ar.SetError();
	if (FileVersion >= 2)
	{
		Ar << S.bLowPosture << S.CapsuleHalfHeightCm << S.LowBreathingHeightCm;
		if (!FMath::IsFinite(S.CapsuleHalfHeightCm) || !FMath::IsFinite(S.LowBreathingHeightCm)) Ar.SetError();
	}
}

void DoorSnapshot(FArchive& Ar, FYUFSTimelineDoorSnapshot& S)
{
	NameField(Ar, S.DoorId);
	Ar << S.OpenFraction << S.SwingDirection << S.bVisible;
	if (!FMath::IsFinite(S.OpenFraction) || !FMath::IsFinite(S.SwingDirection)) Ar.SetError();
}

void ExtinguisherSnapshot(FArchive& Ar, FYUFSTimelineExtinguisherSnapshot& S)
{
	NameField(Ar, S.ToolId);
	Ar << S.Transform;
	EnumField(Ar, S.State);
	Ar << S.RemainingAgent << S.bVisible << S.bSprayVisible << S.SprayRelativeTransform;
	if (S.Transform.ContainsNaN() || S.SprayRelativeTransform.ContainsNaN() || !FMath::IsFinite(S.RemainingAgent)) Ar.SetError();
}

template<typename T, typename F> bool ArrayField(FArchive& Ar, TArray<T>& Items, F&& Element)
{
	int32 Num = Items.Num();
	Ar << Num;
	if (Num < 0 || Num > 4096 || (Ar.IsLoading() && Num > (Ar.TotalSize() - Ar.Tell()) / 8)) return false;
	if (Ar.IsLoading()) Items.SetNum(Num);
	for (T& Item : Items) { Element(Ar, Item); if (Ar.IsError()) return false; }
	return true;
}

bool SerializeFrames(FArchive& Ar, TArray<FYUFSTimelineFrame>& Frames, uint32 FileVersion)
{
	int32 Count = Frames.Num();
	Ar << Count;
	if (Count <= 0 || Count > 1000000 || (Ar.IsLoading() && Count > Ar.TotalSize() / 20)) return false;
	if (Ar.IsLoading()) Frames.SetNum(Count);
	float PreviousTime = -TNumericLimits<float>::Max();
	for (auto& Frame : Frames)
	{
		Ar << Frame.FireElapsedTime << Frame.FireFrame << Frame.EvacuatedCount << Frame.IncapacitatedCount;
		if (!FMath::IsFinite(Frame.FireElapsedTime) || Frame.FireElapsedTime < PreviousTime) return false;
		PreviousTime = Frame.FireElapsedTime;
		int32 NpcCount = Frame.NPCSnapshots.Num();
		Ar << NpcCount;
		if (NpcCount < 0 || NpcCount > 4096 || (Ar.IsLoading() && NpcCount > (Ar.TotalSize() - Ar.Tell()) / 128)) return false;
		if (Ar.IsLoading()) Frame.NPCSnapshots.SetNum(NpcCount);
		for (auto& Npc : Frame.NPCSnapshots) { Snapshot(Ar, Npc, FileVersion); if (Ar.IsError()) return false; }
		if (FileVersion >= 2)
		{
			Ar << Frame.TotalNPCCount << Frame.SimulationElapsedTime;
			if (!FMath::IsFinite(Frame.SimulationElapsedTime)) return false;
			if (!ArrayField(Ar, Frame.DoorSnapshots, DoorSnapshot)) return false;
			if (!ArrayField(Ar, Frame.ExtinguisherSnapshots, ExtinguisherSnapshot)) return false;
		}
		if (Ar.Tell() > MaxBytes) return false;
	}
	return !Ar.IsError();
}
}

bool YUFSReplayArchive::Exists(const FString& RecordingId)
{
	const FString Path = RecordingPath(RecordingId);
	return !Path.IsEmpty() && IFileManager::Get().FileExists(*Path);
}

bool YUFSReplayArchive::Save(const TArray<FYUFSTimelineFrame>& Frames, FString& OutRecordingId)
{
	OutRecordingId.Reset();
	TArray<uint8> Raw;
	FMemoryWriter Writer(Raw);
	// Saving serializers never mutate the source; avoid duplicating a full run.
	if (!SerializeFrames(Writer, const_cast<TArray<FYUFSTimelineFrame>&>(Frames), Version)) return false;
	int32 RawSize = Raw.Num();
	int32 CompressedSize = FCompression::CompressMemoryBound(NAME_Zlib, RawSize);
	TArray<uint8> Compressed;
	Compressed.SetNumUninitialized(CompressedSize);
	if (!FCompression::CompressMemory(NAME_Zlib, Compressed.GetData(), CompressedSize, Raw.GetData(), RawSize)) return false;
	uint32 HeaderMagic = Magic, HeaderVersion = Version, Checksum = FCrc::MemCrc32(Raw.GetData(), Raw.Num());
	TArray<uint8> File;
	FMemoryWriter FileWriter(File);
	FileWriter << HeaderMagic << HeaderVersion << RawSize << CompressedSize << Checksum;
	FileWriter.Serialize(Compressed.GetData(), CompressedSize);
	const FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Path = RecordingPath(Id);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	if (!FFileHelper::SaveArrayToFile(File, *Path)) return false;
	OutRecordingId = Id;
	return true;
}

bool YUFSReplayArchive::Load(const FString& RecordingId, TArray<FYUFSTimelineFrame>& OutFrames)
{
	OutFrames.Reset();
	const FString Path = RecordingPath(RecordingId);
	if (Path.IsEmpty()) return false;
	const int64 FileSize = IFileManager::Get().FileSize(*Path);
	if (FileSize < 20 || FileSize > MaxBytes) return false;
	TArray<uint8> File;
	if (!FFileHelper::LoadFileToArray(File, *Path)) return false;
	FMemoryReader FileReader(File);
	uint32 HeaderMagic = 0, HeaderVersion = 0, Checksum = 0;
	int32 RawSize = 0, CompressedSize = 0;
	FileReader << HeaderMagic << HeaderVersion << RawSize << CompressedSize << Checksum;
	if (HeaderMagic != Magic || HeaderVersion < MinVersion || HeaderVersion > Version || RawSize <= 0 || RawSize > MaxBytes
		|| CompressedSize <= 0 || CompressedSize != File.Num() - FileReader.Tell()) return false;
	TArray<uint8> Raw;
	Raw.SetNumUninitialized(RawSize);
	if (!FCompression::UncompressMemory(NAME_Zlib, Raw.GetData(), RawSize,
		File.GetData() + FileReader.Tell(), CompressedSize)) return false;
	if (FCrc::MemCrc32(Raw.GetData(), Raw.Num()) != Checksum) return false;
	FMemoryReader Reader(Raw);
	if (!SerializeFrames(Reader, OutFrames, HeaderVersion) || Reader.Tell() != Reader.TotalSize())
	{
		OutFrames.Reset();
		return false;
	}
	return true;
}
