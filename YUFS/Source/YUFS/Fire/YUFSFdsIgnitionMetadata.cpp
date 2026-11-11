#include "Fire/YUFSFdsIgnitionMetadata.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	constexpr int64 MaxMetadataBytes = 1024 * 1024;
	constexpr double AxisTolerance = 1.e-4;

	bool FiniteVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	bool ReadFiniteNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, double& Out)
	{
		if (!Object.IsValid()) return false;
		const TSharedPtr<FJsonValue> Value = Object->TryGetField(Key);
		// Json's convenience getters coerce booleans and numeric strings. The contract must not.
		return Value.IsValid() && Value->Type == EJson::Number && Value->TryGetNumber(Out) && FMath::IsFinite(Out);
	}

	bool ReadString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, FString& Out)
	{
		const TSharedPtr<FJsonValue> Value = Object->TryGetField(Key);
		return Value.IsValid() && Value->Type == EJson::String && Value->TryGetString(Out);
	}

	bool ReadVector(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, FVector& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Object.IsValid() || !Object->TryGetArrayField(Key, Values) || !Values || Values->Num() != 3) return false;
		double Components[3];
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const TSharedPtr<FJsonValue>& Value = (*Values)[Index];
			if (!Value.IsValid() || Value->Type != EJson::Number
				|| !Value->TryGetNumber(Components[Index]) || !FMath::IsFinite(Components[Index])) return false;
		}
		Out = FVector(Components[0], Components[1], Components[2]);
		return true;
	}

	bool ReadObject(const TSharedPtr<FJsonObject>& Parent, const TCHAR* Key, TSharedPtr<FJsonObject>& Out)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!Parent->TryGetObjectField(Key, Object) || !Object || !Object->IsValid()) return false;
		Out = *Object;
		return true;
	}

	bool ReadNonEmptyString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, FString& Out)
	{
		if (!ReadString(Object, Key, Out)) return false;
		Out.TrimStartAndEndInline();
		return !Out.IsEmpty();
	}

	bool IsSha256(const FString& Hash)
	{
		if (Hash.Len() != 64) return false;
		for (const TCHAR Character : Hash)
		{
			if (!((Character >= '0' && Character <= '9')
				|| (Character >= 'a' && Character <= 'f') || (Character >= 'A' && Character <= 'F'))) return false;
		}
		return true;
	}
}

bool FYUFSFdsIgnitionMetadata::ParseJson(const FString& Json, FYUFSFdsIgnitionMetadata& OutMetadata, FString& OutReason)
{
	OutMetadata = FYUFSFdsIgnitionMetadata();
	OutReason.Reset();
	const auto Fail = [&OutReason](const TCHAR* Reason) { OutReason = Reason; return false; };
	if (Json.IsEmpty() || Json.Len() > MaxMetadataBytes) return Fail(TEXT("FdsMetadata.EmptyOrTooLarge"));
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return Fail(TEXT("FdsMetadata.InvalidJson"));

	FYUFSFdsIgnitionMetadata Candidate;
	double SchemaVersion = 0.0;
	if (!ReadFiniteNumber(Root, TEXT("schemaVersion"), SchemaVersion) || SchemaVersion != 1.0)
		return Fail(TEXT("FdsMetadata.UnsupportedSchema"));
	if (!ReadNonEmptyString(Root, TEXT("sourceFile"), Candidate.SourceFile)
		|| !ReadNonEmptyString(Root, TEXT("sourceSha256"), Candidate.SourceSha256)
		|| !IsSha256(Candidate.SourceSha256)) return Fail(TEXT("FdsMetadata.MissingSourceProvenance"));
	bool bAlignmentConfirmed = false;
	const TSharedPtr<FJsonValue> Alignment = Root->TryGetField(TEXT("alignmentConfirmed"));
	if (!Alignment.IsValid() || Alignment->Type != EJson::Boolean
		|| !Alignment->TryGetBool(bAlignmentConfirmed) || !bAlignmentConfirmed)
		return Fail(TEXT("FdsMetadata.AlignmentUnconfirmed"));
	FString Units;
	if (!ReadString(Root, TEXT("units"), Units) || Units != TEXT("m"))
		return Fail(TEXT("FdsMetadata.ExpectedMeters"));

	TSharedPtr<FJsonObject> Mapping;
	FVector Origin, AxisX, AxisY, AxisZ, Scale;
	if (!ReadObject(Root, TEXT("coordinateMapping"), Mapping)
		|| !ReadVector(Mapping, TEXT("originUeCm"), Origin)
		|| !ReadVector(Mapping, TEXT("axisX"), AxisX)
		|| !ReadVector(Mapping, TEXT("axisY"), AxisY)
		|| !ReadVector(Mapping, TEXT("axisZ"), AxisZ)
		|| !ReadVector(Mapping, TEXT("scaleCmPerMeter"), Scale))
		return Fail(TEXT("FdsMetadata.InvalidCoordinateMapping"));
	if (Scale.X <= 0.0 || Scale.Y <= 0.0 || Scale.Z <= 0.0)
		return Fail(TEXT("FdsMetadata.InvalidScale"));
	// Reflections and non-uniform positive scales are explicit and supported. Shear is not.
	if (FMath::Abs(AxisX.SizeSquared() - 1.0) > AxisTolerance
		|| FMath::Abs(AxisY.SizeSquared() - 1.0) > AxisTolerance
		|| FMath::Abs(AxisZ.SizeSquared() - 1.0) > AxisTolerance
		|| FMath::Abs(FVector::DotProduct(AxisX, AxisY)) > AxisTolerance
		|| FMath::Abs(FVector::DotProduct(AxisX, AxisZ)) > AxisTolerance
		|| FMath::Abs(FVector::DotProduct(AxisY, AxisZ)) > AxisTolerance)
		return Fail(TEXT("FdsMetadata.NonOrthonormalAxes"));

	TSharedPtr<FJsonObject> TimeMapping;
	FString ReferenceClock;
	if (!ReadObject(Root, TEXT("timeMapping"), TimeMapping)
		|| !ReadString(TimeMapping, TEXT("referenceClock"), ReferenceClock)
		|| ReferenceClock != TEXT("simulationElapsedSeconds")
		|| !ReadFiniteNumber(TimeMapping, TEXT("fdsTimeAtSimulationZeroSeconds"), Candidate.FdsTimeAtSimulationZeroSeconds)
		|| !ReadFiniteNumber(TimeMapping, TEXT("fdsSecondsPerSimulationSecond"), Candidate.FdsSecondsPerSimulationSecond)
		|| Candidate.FdsSecondsPerSimulationSecond <= 0.0)
		return Fail(TEXT("FdsMetadata.InvalidTimeMapping"));

	const TArray<TSharedPtr<FJsonValue>>* Sources = nullptr;
	if (!Root->TryGetArrayField(TEXT("ignitionSources"), Sources) || !Sources || Sources->IsEmpty())
		return Fail(TEXT("FdsMetadata.NoIgnitionSources"));
	FString RequestedId;
	if (Root->HasField(TEXT("selectedIgnitionId")) && !ReadNonEmptyString(Root, TEXT("selectedIgnitionId"), RequestedId))
		return Fail(TEXT("FdsMetadata.InvalidSelectedIgnitionId"));
	if (Sources->Num() > 1 && RequestedId.IsEmpty()) return Fail(TEXT("FdsMetadata.AmbiguousIgnitionSource"));

	TSet<FString> SeenIds;
	bool bSelected = false;
	for (const TSharedPtr<FJsonValue>& SourceValue : *Sources)
	{
		if (!SourceValue.IsValid() || SourceValue->Type != EJson::Object)
			return Fail(TEXT("FdsMetadata.InvalidIgnitionSource"));
		const TSharedPtr<FJsonObject> Source = SourceValue->AsObject();
		FString Id;
		FVector PositionMeters;
		double ActivationTime = 0.0;
		if (!Source.IsValid() || !ReadNonEmptyString(Source, TEXT("id"), Id)
			|| !ReadVector(Source, TEXT("fdsPositionMeters"), PositionMeters)
			|| !ReadFiniteNumber(Source, TEXT("activationTimeSeconds"), ActivationTime) || ActivationTime < 0.0)
			return Fail(TEXT("FdsMetadata.InvalidIgnitionSource"));
		if (SeenIds.Contains(Id)) return Fail(TEXT("FdsMetadata.DuplicateIgnitionId"));
		SeenIds.Add(Id);
		const FVector Converted = Origin + AxisX * (Scale.X * PositionMeters.X)
			+ AxisY * (Scale.Y * PositionMeters.Y) + AxisZ * (Scale.Z * PositionMeters.Z);
		if (!FiniteVector(Converted)) return Fail(TEXT("FdsMetadata.NonFiniteWorldPosition"));
		if (Id == RequestedId || (RequestedId.IsEmpty() && Sources->Num() == 1))
		{
			Candidate.SelectedIgnitionId = Id;
			Candidate.ActivationTimeSeconds = ActivationTime;
			Candidate.WorldLocation = Converted;
			bSelected = true;
		}
	}
	if (!bSelected) return Fail(TEXT("FdsMetadata.SelectedIgnitionNotFound"));
	Candidate.SourceSha256.ToLowerInline();
	Candidate.bValid = true;
	OutMetadata = MoveTemp(Candidate);
	return true;
}

bool FYUFSFdsIgnitionMetadata::LoadFile(const FString& Path, FYUFSFdsIgnitionMetadata& OutMetadata, FString& OutReason)
{
	OutMetadata = FYUFSFdsIgnitionMetadata();
	OutReason.Reset();
	const int64 Size = IFileManager::Get().FileSize(*Path);
	if (Path.IsEmpty() || Size <= 0 || Size > MaxMetadataBytes)
	{
		OutReason = TEXT("FdsMetadata.FileMissingEmptyOrTooLarge");
		return false;
	}
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		OutReason = TEXT("FdsMetadata.FileReadFailed");
		return false;
	}
	return ParseJson(Json, OutMetadata, OutReason);
}

bool FYUFSFdsIgnitionMetadata::TryGetActiveIgnition(double SimulationTimeSeconds, FVector& OutWorldLocation, FString& OutReason) const
{
	OutWorldLocation = FVector::ZeroVector;
	OutReason.Reset();
	if (!bValid) { OutReason = TEXT("FdsMetadata.Unavailable"); return false; }
	if (!FMath::IsFinite(SimulationTimeSeconds) || SimulationTimeSeconds < 0.0)
	{
		OutReason = TEXT("FdsMetadata.InvalidSimulationTime");
		return false;
	}
	const double FdsTime = FdsTimeAtSimulationZeroSeconds + SimulationTimeSeconds * FdsSecondsPerSimulationSecond;
	if (!FMath::IsFinite(FdsTime)) { OutReason = TEXT("FdsMetadata.NonFiniteFdsTime"); return false; }
	if (FdsTime < ActivationTimeSeconds) { OutReason = TEXT("FdsMetadata.IgnitionNotActiveYet"); return false; }
	OutWorldLocation = WorldLocation;
	return true;
}
