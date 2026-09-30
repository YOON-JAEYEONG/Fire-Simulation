#include "Simulation/YUFSTimelineRecorder.h"

#include "Fire/YUFSHeterogeneousVolume.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Integration/YUFSBelongingsRetrievalComponent.h"

namespace
{
	// 관찰 모드에서 "가방을 메고 나가는 장면"을 찾기 쉽도록, 그 장면이 처음 기록된 슬라이더 시각을 남깁니다.
	void LogBelongingsReplayMoments(const TArray<FYUFSTimelineFrame>& Frames)
	{
		const uint8 Carrying = static_cast<uint8>(EYUFSBelongingsRetrievalPhase::Carrying);
		TMap<FName, float> FirstSeenCarrying;
		for (const FYUFSTimelineFrame& Frame : Frames)
		{
			for (const FYUFSTimelineNPCSnapshot& Snapshot : Frame.NPCSnapshots)
			{
				if (Snapshot.BelongingsPhase == Carrying && Snapshot.bVisible && !FirstSeenCarrying.Contains(Snapshot.NPCId))
				{
					FirstSeenCarrying.Add(Snapshot.NPCId, Frame.FireElapsedTime);
				}
			}
		}
		for (const TPair<FName, float>& Moment : FirstSeenCarrying)
		{
			UE_LOG(LogTemp, Display, TEXT("[NPCBelongings] Replay: %s carries its bag from %.0fs on the timeline"),
				*Moment.Key.ToString(), Moment.Value);
		}
		if (FirstSeenCarrying.IsEmpty())
		{
			UE_LOG(LogTemp, Display, TEXT("[NPCBelongings] Replay: nobody carried a bag in this run"));
		}
	}
}

UYUFSTimelineRecorder::UYUFSTimelineRecorder()
{
	// 직접 Tick하지 않고, SimulationController가 현재 Phase에 맞춰 명시적으로 호출합니다.
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
	TargetFireTimeSeconds = FMath::Max(0.f, InTargetFireTimeSeconds);
	RecordIntervalSeconds = FMath::Max(0.05f, InRecordIntervalSeconds);
	TimeSinceLastRecord = RecordIntervalSeconds; // 시작 직후 첫 프레임을 바로 저장하기 위한 값
	CurrentReviewFireTime = 0.f;

	UE_LOG(LogTemp, Log, TEXT("[YUFSTimeline] Recording started. Target=%.2fs, Interval=%.2fs"),
		TargetFireTimeSeconds, RecordIntervalSeconds);
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
	if (!bRecording)
	{
		return;
	}

	TimeSinceLastRecord += DeltaTime;
	if (TimeSinceLastRecord < RecordIntervalSeconds && FireElapsedTime < TargetFireTimeSeconds)
	{
		return;
	}

	TimeSinceLastRecord = 0.f;
	CaptureFrame(FireElapsedTime, FireFrame, NPCs, EvacuatedCount, IncapacitatedCount);
}

void UYUFSTimelineRecorder::CaptureFrame(
	float FireElapsedTime,
	int32 FireFrame,
	const TArray<AYUFSEvacuationNPC*>& NPCs,
	int32 EvacuatedCount,
	int32 IncapacitatedCount)
{
	FYUFSTimelineFrame Frame;
	Frame.FireElapsedTime = FMath::Max(0.f, FireElapsedTime);
	Frame.FireFrame = FMath::Max(0, FireFrame);
	Frame.EvacuatedCount = EvacuatedCount;
	Frame.IncapacitatedCount = IncapacitatedCount;
	Frame.NPCSnapshots.Reserve(NPCs.Num());
	
	for (AYUFSEvacuationNPC* NPC : NPCs)
	{
		if (!IsValid(NPC))
		{
			continue;
		}

		Frame.NPCSnapshots.Add(NPC->BuildTimelineSnapshot());
	}

	RecordedFrames.Add(MoveTemp(Frame));
}

void UYUFSTimelineRecorder::EnterReviewMode(const TArray<AYUFSEvacuationNPC*>& NPCs)
{
	StopRecording();
	bReviewPlaying = false;
	LastAppliedReviewFrame = nullptr;

	for (AYUFSEvacuationNPC* NPC : NPCs)
	{
		if (IsValid(NPC))
		{
			NPC->SetTimelinePlaybackMode(true);
		}
	}

	// 기록된 마지막 시점부터 관찰을 시작합니다.
	if (!RecordedFrames.IsEmpty())
	{
		CurrentReviewFireTime = RecordedFrames.Last().FireElapsedTime;
		ApplyFrame(RecordedFrames.Last(), NPCs);
	}

	UE_LOG(LogTemp, Warning, TEXT("[YUFSTimeline] Review mode entered. Frames=%d, MaxTime=%.2fs"),
		RecordedFrames.Num(), GetMaxRecordedFireTime());
	LogBelongingsReplayMoments(RecordedFrames);
}

void UYUFSTimelineRecorder::PlayReview()
{
	if (RecordedFrames.Num() <= 1)
	{
		return;
	}

	bReviewPlaying = true;
	if (HeterogeneousVolume)
	{
		HeterogeneousVolume->ResumeFire();
	}
}

void UYUFSTimelineRecorder::PauseReview()
{
	bReviewPlaying = false;
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

	CurrentReviewFireTime += DeltaTime;
	const float MaxTime = GetMaxRecordedFireTime();
	if (CurrentReviewFireTime >= MaxTime)
	{
		CurrentReviewFireTime = MaxTime;
		PauseReview();
	}

	SeekToFireTime(CurrentReviewFireTime, NPCs);
}

void UYUFSTimelineRecorder::SeekToFireTime(float TargetFireTime, const TArray<AYUFSEvacuationNPC*>& NPCs)
{
	if (RecordedFrames.IsEmpty())
	{
		return;
	}

	CurrentReviewFireTime = FMath::Clamp(TargetFireTime, 0.f, GetMaxRecordedFireTime());

	const FYUFSTimelineFrame* Frame = FindNearestFrame(CurrentReviewFireTime);
	if (Frame && Frame != LastAppliedReviewFrame)
	{
		ApplyFrame(*Frame, NPCs);
		LastAppliedReviewFrame = Frame;
	}
}

float UYUFSTimelineRecorder::GetMaxRecordedFireTime() const
{
	return RecordedFrames.IsEmpty() ? 0.f : RecordedFrames.Last().FireElapsedTime;
}

float UYUFSTimelineRecorder::GetTimelineProgress01() const
{
	const float MaxTime = GetMaxRecordedFireTime();
	if (MaxTime <= KINDA_SMALL_NUMBER)
	{
		return 0.f;
	}
	return FMath::Clamp(CurrentReviewFireTime / MaxTime, 0.f, 1.f);
}

const FYUFSTimelineFrame* UYUFSTimelineRecorder::FindNearestFrame(float TargetFireTime) const
{
	if (RecordedFrames.IsEmpty())
	{
		return nullptr;
	}

	int32 BestIndex = 0;
	float BestDiff = TNumericLimits<float>::Max();

	for (int32 i = 0; i < RecordedFrames.Num(); ++i)
	{
		const float Diff = FMath::Abs(RecordedFrames[i].FireElapsedTime - TargetFireTime);
		if (Diff < BestDiff)
		{
			BestDiff = Diff;
			BestIndex = i;
		}
	}

	return &RecordedFrames[BestIndex];
}

void UYUFSTimelineRecorder::ApplyFrame(const FYUFSTimelineFrame& Frame, const TArray<AYUFSEvacuationNPC*>& NPCs)
{
	// 화재 데이터 전체를 저장하지 않고, 프레임 번호만 HeterogeneousVolume에 적용합니다.
	if (HeterogeneousVolume)
	{
		HeterogeneousVolume->SetFrame(Frame.FireFrame);
		HeterogeneousVolume->PauseFire();
	}

	TMap<FName, const FYUFSTimelineNPCSnapshot*> SnapshotById;
	for (const FYUFSTimelineNPCSnapshot& Snapshot : Frame.NPCSnapshots)
	{
		SnapshotById.Add(Snapshot.NPCId, &Snapshot);
	}

	for (AYUFSEvacuationNPC* NPC : NPCs)
	{
		if (!IsValid(NPC))
		{
			continue;
		}

		const FYUFSTimelineNPCSnapshot* const* Found = SnapshotById.Find(NPC->GetFName());
		if (Found && *Found)
		{
			NPC->ApplyTimelineSnapshot(**Found);
		}
	}
}
