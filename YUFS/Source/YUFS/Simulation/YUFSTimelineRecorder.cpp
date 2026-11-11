#include "Simulation/YUFSTimelineRecorder.h"
#include "Simulation/YUFSSimulationController.h"

#include "Fire/YUFSHeterogeneousVolume.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "Fire/YUFSInteractionDoor.h"
#include "Fire/YUFSFireExtinguisher.h"
#include "EngineUtils.h"

namespace
{
	struct FTimelineSnapshotLookup
	{
		TMap<int32, const FYUFSTimelineNPCSnapshot*> ByStableId;
		TMap<FName, const FYUFSTimelineNPCSnapshot*> ByName;

		explicit FTimelineSnapshotLookup(const TArray<FYUFSTimelineNPCSnapshot>& Snapshots)
		{
			ByStableId.Reserve(Snapshots.Num());
			ByName.Reserve(Snapshots.Num());
			for (const FYUFSTimelineNPCSnapshot& Snapshot : Snapshots)
			{
				if (Snapshot.StableNPCId != INDEX_NONE)
				{
					ByStableId.Add(Snapshot.StableNPCId, &Snapshot);
				}
				ByName.Add(Snapshot.NPCId, &Snapshot);
			}
		}

		const FYUFSTimelineNPCSnapshot* Find(int32 StableId, FName Name) const
		{
			if (StableId != INDEX_NONE)
			{
				if (const FYUFSTimelineNPCSnapshot* const* Found = ByStableId.Find(StableId))
				{
					return *Found;
				}
			}
			const FYUFSTimelineNPCSnapshot* const* Found = ByName.Find(Name);
			// Actor names can be reused by a different resident after a map reload.
			// Name fallback is valid only for legacy snapshots without stable identity.
			return Found && (StableId == INDEX_NONE || (*Found)->StableNPCId == INDEX_NONE) ? *Found : nullptr;
		}
	};

	void InterpolateSnapshot(
		FYUFSTimelineNPCSnapshot& Result,
		const FYUFSTimelineNPCSnapshot& Upper,
		float Alpha,
		float FrameDuration)
	{
		// Discrete state remains that of the lower sample until the upper timestamp.
		Result.Location = FMath::Lerp(Result.Location, Upper.Location, Alpha);
		Result.Rotation = FQuat::Slerp(Result.Rotation.Quaternion(), Upper.Rotation.Quaternion(), Alpha).GetNormalized().Rotator();
		Result.Velocity = FMath::Lerp(Result.Velocity, Upper.Velocity, Alpha);

		const bool bSameAnimation = Result.bHasAnimationState && Upper.bHasAnimationState
			&& Result.AnimationAssetPath == Upper.AnimationAssetPath
			&& Result.BehaviorState == Upper.BehaviorState
			&& Result.CurrentAction == Upper.CurrentAction
			&& Result.DisplayAction == Upper.DisplayAction
			&& Result.bVisible == Upper.bVisible
			&& Result.bEvacuated == Upper.bEvacuated
			&& Result.bIncapacitated == Upper.bIncapacitated
			&& Result.bAnimationLooping == Upper.bAnimationLooping;
		if (!bSameAnimation)
		{
			return;
		}

		const float ClipLength = Result.AnimationLengthSeconds;
		float PhaseAdvance = Upper.AnimationPositionSeconds - Result.AnimationPositionSeconds;
		if (Result.bAnimationLooping && ClipLength > SMALL_NUMBER)
		{
			// Unwrap a forward loop before interpolation. Use the recorded rate to
			// recover additional full loops if a slow simulation frame spans a clip.
			if (PhaseAdvance < 0.f)
			{
				PhaseAdvance += ClipLength;
			}
			const float ExpectedAdvance = FrameDuration * FMath::Max(0.f,
				0.5f * (Result.AnimationPlayRate + Upper.AnimationPlayRate));
			const int32 ExtraLoops = FMath::Max(0, FMath::RoundToInt((ExpectedAdvance - PhaseAdvance) / ClipLength));
			PhaseAdvance += ExtraLoops * ClipLength;
			Result.AnimationPositionSeconds = FMath::Fmod(Result.AnimationPositionSeconds + Alpha * PhaseAdvance, ClipLength);
		}
		else if (PhaseAdvance >= 0.f)
		{
			Result.AnimationPositionSeconds += Alpha * PhaseAdvance;
		}
		Result.AnimationPlayRate = FMath::Lerp(Result.AnimationPlayRate, Upper.AnimationPlayRate, Alpha);
	}
}

UYUFSTimelineRecorder::UYUFSTimelineRecorder()
{
	// The controller records after NPC movement and animation have ticked.
	PrimaryComponentTick.bCanEverTick = false;
}

void UYUFSTimelineRecorder::Initialize(AYUFSSimulationController* InController, AYUFSHeterogeneousVolume* InVolume)
{
	OwnerController = InController;
	HeterogeneousVolume = InVolume;
}

void UYUFSTimelineRecorder::BeginRecording(float InTargetFireTimeSeconds, float InRecordIntervalSeconds)
{
	RecordedFrames.Empty();
	LastAppliedReviewFrame = nullptr;
	bRecording = true;
	bReviewPlaying = false;
	bRestartReviewOnNextTick = false;
	CurrentReviewFireTime = 0.f;
	CurrentEvacuatedCount = 0;
	CurrentIncapacitatedCount = 0;

	// Keep the interval argument for existing callers, but never discard observed
	// movement/animation samples. The controller owns the recording end condition.
	UE_LOG(LogTemp, Log, TEXT("[YUFSTimeline] Recording every simulation frame. Target=%.2fs, LegacyInterval=%.2fs"),
		InTargetFireTimeSeconds, InRecordIntervalSeconds);
}

void UYUFSTimelineRecorder::LoadRecording(const TArray<FYUFSTimelineFrame>& Frames)
{
	RecordedFrames = Frames;
	LastAppliedReviewFrame = nullptr;
	RecordedFrames.StableSort([](const FYUFSTimelineFrame& A, const FYUFSTimelineFrame& B)
	{
		return A.FireElapsedTime < B.FireElapsedTime;
	});
	bRecording = false;
	bReviewPlaying = false;
	bRestartReviewOnNextTick = false;
	CurrentReviewFireTime = GetMinRecordedFireTime();
	CurrentEvacuatedCount = RecordedFrames.IsEmpty() ? 0 : RecordedFrames[0].EvacuatedCount;
	CurrentIncapacitatedCount = RecordedFrames.IsEmpty() ? 0 : RecordedFrames[0].IncapacitatedCount;
}

void UYUFSTimelineRecorder::StopRecording()
{
	bRecording = false;
	UE_LOG(LogTemp, Log, TEXT("[YUFSTimeline] Recording stopped. Captured frames=%d"), RecordedFrames.Num());
}

void UYUFSTimelineRecorder::TickRecording(
	float DeltaTime,
	float FireElapsedTime,
	int32 FireFrame,
	const TArray<AYUFSEvacuationNPC*>& NPCs,
	int32 EvacuatedCount,
	int32 IncapacitatedCount)
{
	if (bRecording)
	{
		CaptureFrame(FireElapsedTime, FireFrame, NPCs, EvacuatedCount, IncapacitatedCount);
	}
}

void UYUFSTimelineRecorder::CaptureFrame(
	float FireElapsedTime,
	int32 FireFrame,
	const TArray<AYUFSEvacuationNPC*>& NPCs,
	int32 EvacuatedCount,
	int32 IncapacitatedCount)
{
	FYUFSTimelineFrame Frame;
	Frame.FireElapsedTime = FireElapsedTime;
	Frame.FireFrame = FMath::Max(0, FireFrame);
	Frame.EvacuatedCount = EvacuatedCount;
	Frame.IncapacitatedCount = IncapacitatedCount;
	Frame.NPCSnapshots.Reserve(NPCs.Num());
	for (AYUFSEvacuationNPC* NPC : NPCs)
	{
		if (IsValid(NPC))
		{
			Frame.NPCSnapshots.Add(NPC->BuildTimelineSnapshot());
		}
	}
	Frame.TotalNPCCount = OwnerController ? OwnerController->GetTotalNPCCount()
		: FMath::Max(Frame.NPCSnapshots.Num(), EvacuatedCount + IncapacitatedCount);
	Frame.SimulationElapsedTime = OwnerController ? OwnerController->GetElapsedTime() : Frame.FireElapsedTime;

	for (TActorIterator<AYUFSInteractionDoor> It(GetWorld()); It; ++It)
		Frame.DoorSnapshots.Add(It->BuildTimelineSnapshot());
	for (TActorIterator<AYUFSFireExtinguisher> It(GetWorld()); It; ++It)
		Frame.ExtinguisherSnapshots.Add(It->BuildTimelineSnapshot());
	if (!RecordedFrames.IsEmpty() && RecordedFrames.Last().FireElapsedTime == Frame.FireElapsedTime)
		RecordedFrames.Last() = MoveTemp(Frame);
	else
		RecordedFrames.Add(MoveTemp(Frame));
}

void UYUFSTimelineRecorder::EnterReviewMode(const TArray<AYUFSEvacuationNPC*>& NPCs)
{
	StopRecording();
	bReviewPlaying = false;
	bRestartReviewOnNextTick = false;
	for (AYUFSEvacuationNPC* NPC : NPCs)
	{
		if (IsValid(NPC))
		{
			NPC->SetTimelinePlaybackMode(true);
		}
	}

	// Review initially displays the simulation's completed state.
	SeekToFireTime(GetMaxRecordedFireTime(), NPCs);
	UE_LOG(LogTemp, Log, TEXT("[YUFSTimeline] Review mode entered. Frames=%d, TimeRange=[%.2f, %.2f]s"),
		RecordedFrames.Num(), GetMinRecordedFireTime(), GetMaxRecordedFireTime());
}

void UYUFSTimelineRecorder::PlayReview()
{
	if (RecordedFrames.Num() <= 1)
	{
		return;
	}
	bRestartReviewOnNextTick = CurrentReviewFireTime >= GetMaxRecordedFireTime();
	bReviewPlaying = true;
	if (HeterogeneousVolume)
	{
		// The review cursor owns fire time, just as it owns NPC/prop poses.
		HeterogeneousVolume->PauseFire();
	}
}

void UYUFSTimelineRecorder::PauseReview()
{
	bReviewPlaying = false;
	bRestartReviewOnNextTick = false;
	if (HeterogeneousVolume)
	{
		HeterogeneousVolume->PauseFire();
	}
}

void UYUFSTimelineRecorder::TickReview(float DeltaTime, const TArray<AYUFSEvacuationNPC*>& NPCs)
{
	if (!bReviewPlaying || RecordedFrames.IsEmpty())
	{
		return;
	}
	if (bRestartReviewOnNextTick)
	{
		bRestartReviewOnNextTick = false;
		SeekToFireTime(GetMinRecordedFireTime(), NPCs);
		return;
	}

	const float NextTime = FMath::Min(CurrentReviewFireTime + FMath::Max(0.f, DeltaTime), GetMaxRecordedFireTime());
	SeekToFireTime(NextTime, NPCs);
	if (NextTime >= GetMaxRecordedFireTime())
	{
		PauseReview();
	}
}

void UYUFSTimelineRecorder::SeekToFireTime(float TargetFireTime, const TArray<AYUFSEvacuationNPC*>& NPCs)
{
	if (RecordedFrames.IsEmpty())
	{
		return;
	}
	bRestartReviewOnNextTick = false;
	CurrentReviewFireTime = FMath::Clamp(TargetFireTime, GetMinRecordedFireTime(), GetMaxRecordedFireTime());
	int32 LowerIndex;
	int32 UpperIndex;
	FindBracketingFrames(CurrentReviewFireTime, LowerIndex, UpperIndex);
	const FYUFSTimelineFrame& Lower = RecordedFrames[LowerIndex];
	const FYUFSTimelineFrame& Upper = RecordedFrames[UpperIndex];
	const float Duration = Upper.FireElapsedTime - Lower.FireElapsedTime;
	const float Alpha = Duration > SMALL_NUMBER ? (CurrentReviewFireTime - Lower.FireElapsedTime) / Duration : 0.f;
	ApplyFrames(Lower, Upper, Alpha, NPCs);
	LastAppliedReviewFrame = &Lower;
}

float UYUFSTimelineRecorder::GetMinRecordedFireTime() const
{
	return RecordedFrames.IsEmpty() ? 0.f : RecordedFrames[0].FireElapsedTime;
}

float UYUFSTimelineRecorder::GetMaxRecordedFireTime() const
{
	return RecordedFrames.IsEmpty() ? 0.f : RecordedFrames.Last().FireElapsedTime;
}

float UYUFSTimelineRecorder::GetTimelineProgress01() const
{
	const float MinTime = GetMinRecordedFireTime();
	const float Duration = GetMaxRecordedFireTime() - MinTime;
	return Duration > SMALL_NUMBER ? FMath::Clamp((CurrentReviewFireTime - MinTime) / Duration, 0.f, 1.f) : 0.f;
}

void UYUFSTimelineRecorder::FindBracketingFrames(float TargetFireTime, int32& OutLowerIndex, int32& OutUpperIndex) const
{
	// Upper bound selects the last sample at or before the requested timestamp.
	// Therefore a future action or disappearance never leaks into an earlier time.
	int32 First = 0;
	int32 End = RecordedFrames.Num();
	while (First < End)
	{
		const int32 Middle = First + (End - First) / 2;
		if (RecordedFrames[Middle].FireElapsedTime <= TargetFireTime)
		{
			First = Middle + 1;
		}
		else
		{
			End = Middle;
		}
	}
	OutLowerIndex = FMath::Max(0, First - 1);
	OutUpperIndex = FMath::Min(First, RecordedFrames.Num() - 1);
}

void UYUFSTimelineRecorder::ApplyFrames(
	const FYUFSTimelineFrame& LowerFrame,
	const FYUFSTimelineFrame& UpperFrame,
	float Alpha,
	const TArray<AYUFSEvacuationNPC*>& NPCs)
{
	CurrentEvacuatedCount = LowerFrame.EvacuatedCount;
	CurrentIncapacitatedCount = LowerFrame.IncapacitatedCount;
	if (HeterogeneousVolume)
	{
		HeterogeneousVolume->SetActorHiddenInGame(CurrentReviewFireTime < 0.f);
		if (HeterogeneousVolume->GetFrame() != LowerFrame.FireFrame)
		{
			HeterogeneousVolume->SetFrame(LowerFrame.FireFrame);
		}
		HeterogeneousVolume->PauseFire();
	}

	const FTimelineSnapshotLookup LowerLookup(LowerFrame.NPCSnapshots);
	const FTimelineSnapshotLookup UpperLookup(UpperFrame.NPCSnapshots);
	for (AYUFSEvacuationNPC* NPC : NPCs)
	{
		if (!IsValid(NPC))
		{
			continue;
		}
		const FYUFSTimelineNPCSnapshot* Lower = LowerLookup.Find(NPC->GetStableNPCId(), NPC->GetFName());
		if (!Lower)
		{
			// Actors spawned later must not remain visible when scrubbing backwards.
			NPC->SetActorHiddenInGame(true);
			NPC->SetActorEnableCollision(false);
			continue;
		}
		if (Alpha <= 0.f)
		{
			NPC->ApplyTimelineSnapshot(*Lower);
			continue;
		}

		FYUFSTimelineNPCSnapshot Interpolated = *Lower;
		if (const FYUFSTimelineNPCSnapshot* Upper = UpperLookup.Find(Lower->StableNPCId, Lower->NPCId))
		{
			InterpolateSnapshot(Interpolated, *Upper, Alpha, UpperFrame.FireElapsedTime - LowerFrame.FireElapsedTime);
		}
		NPC->ApplyTimelineSnapshot(Interpolated);
	}
	// Legacy (v1) recordings carry no prop state; leave props as they are instead of hiding them.
	TMap<FName, const FYUFSTimelineDoorSnapshot*> Doors;
	for (const auto& Snapshot : LowerFrame.DoorSnapshots) Doors.Add(Snapshot.DoorId, &Snapshot);
	for (TActorIterator<AYUFSInteractionDoor> It(GetWorld()); !LowerFrame.DoorSnapshots.IsEmpty() && It; ++It)
	{
		if (const auto* Found = Doors.Find(It->GetFName())) It->ApplyTimelineSnapshot(**Found);
		else { auto Hidden=It->BuildTimelineSnapshot(); Hidden.bVisible=false; It->ApplyTimelineSnapshot(Hidden); }
	}
	TMap<FName, const FYUFSTimelineExtinguisherSnapshot*> Tools;
	for (const auto& Snapshot : LowerFrame.ExtinguisherSnapshots) Tools.Add(Snapshot.ToolId, &Snapshot);
	for (TActorIterator<AYUFSFireExtinguisher> It(GetWorld()); !LowerFrame.ExtinguisherSnapshots.IsEmpty() && It; ++It)
	{
		if (const auto* Found = Tools.Find(It->GetFName())) It->ApplyTimelineSnapshot(**Found);
		else { auto Hidden=It->BuildTimelineSnapshot(); Hidden.bVisible=false; It->ApplyTimelineSnapshot(Hidden); }
	}
}
