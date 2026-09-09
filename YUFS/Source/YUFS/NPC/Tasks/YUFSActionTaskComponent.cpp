#include "NPC/Tasks/YUFSActionTaskComponent.h"

#include "Core/YUFSDeterministicRng.h"

UYUFSActionTaskComponent::UYUFSActionTaskComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UYUFSActionTaskComponent::UpdateTask(
	float DeltaTime,
	EYUFSAction Action,
	EYUFSIntent Intent,
	bool bImmediateLifeRisk,
	bool bOfficialInstruction,
	FYUFSDeterministicRngSet& RandomSource)
{
	if (!bHasObservedContext || Action != LastObservedAction || Intent != LastObservedIntent)
	{
		CompletedTask = EYUFSActionTask::None;
		LastObservedAction = Action;
		LastObservedIntent = Intent;
		bHasObservedContext = true;
	}

	const bool bIntentAllowsTask =
		Intent == EYUFSIntent::Observe || Intent == EYUFSIntent::Prepare || Intent == EYUFSIntent::Help;
	EYUFSActionTask DesiredTask = bIntentAllowsTask ? MapActionToTask(Action) : EYUFSActionTask::None;
	if (DesiredTask == CompletedTask)
	{
		DesiredTask = EYUFSActionTask::None;
	}

	UpdateResolvedTask(
		DeltaTime,
		DesiredTask,
		bImmediateLifeRisk,
		bOfficialInstruction,
		RandomSource);
}

void UYUFSActionTaskComponent::UpdateDesiredTask(
	float DeltaTime,
	EYUFSActionTask DesiredTask,
	int64 BehaviorDecisionRevision,
	EYUFSIntent Intent,
	bool bImmediateLifeRisk,
	bool bOfficialInstruction,
	FYUFSDeterministicRngSet& RandomSource)
{
	if (!bHasObservedContext
		|| Intent != LastObservedIntent
		|| BehaviorDecisionRevision != LastBehaviorDecisionRevision)
	{
		CompletedTask = EYUFSActionTask::None;
		LastObservedIntent = Intent;
		LastBehaviorDecisionRevision = BehaviorDecisionRevision;
		bHasObservedContext = true;
	}

	const bool bIntentAllowsTask =
		Intent == EYUFSIntent::Observe || Intent == EYUFSIntent::Prepare || Intent == EYUFSIntent::Help;
	if (!bIntentAllowsTask)
	{
		if (!(Intent == EYUFSIntent::CommitEvac && DesiredTask == EYUFSActionTask::InitialExtinguish))
			DesiredTask = EYUFSActionTask::None;
	}
	if (DesiredTask == CompletedTask)
	{
		DesiredTask = EYUFSActionTask::None;
	}

	UpdateResolvedTask(
		DeltaTime,
		DesiredTask,
		bImmediateLifeRisk,
		bOfficialInstruction,
		RandomSource);
}

void UYUFSActionTaskComponent::UpdateResolvedTask(
	float DeltaTime,
	EYUFSActionTask DesiredTask,
	bool bImmediateLifeRisk,
	bool bOfficialInstruction,
	FYUFSDeterministicRngSet& RandomSource)
{

	if (bImmediateLifeRisk)
	{
		CancelTask(EYUFSTaskCancelReason::LifeRisk);
		return;
	}

	if (bOfficialInstruction && CurrentTask != EYUFSActionTask::None)
	{
		CancelTask(EYUFSTaskCancelReason::OfficialInstruction);
		return;
	}

	if (DesiredTask != CurrentTask)
	{
		if (CurrentTask != EYUFSActionTask::None)
		{
			CancelTask(EYUFSTaskCancelReason::IntentChanged);
		}
		if (DesiredTask != EYUFSActionTask::None)
		{
			StartTask(DesiredTask, RandomSource);
		}
	}

	if (CurrentTask == EYUFSActionTask::None)
	{
		return;
	}

	ElapsedSeconds += DeltaTime;
	// Object execution owns suppression completion; a duration draw is not a
	// successful extinguishing operation. The runtime adapter reports feedback.
	if (ElapsedSeconds >= PlannedDurationSeconds && CurrentTask != EYUFSActionTask::InitialExtinguish)
	{
		CompletedTask = CurrentTask;
		CancelTask(EYUFSTaskCancelReason::Completed);
	}
}

float UYUFSActionTaskComponent::GetProgress01() const
{
	return PlannedDurationSeconds > KINDA_SMALL_NUMBER
		? FMath::Clamp(ElapsedSeconds / PlannedDurationSeconds, 0.f, 1.f)
		: 0.f;
}

bool UYUFSActionTaskComponent::ConsumeTaskEvent(
	EYUFSActionTask& OutFrom,
	EYUFSActionTask& OutTo,
	EYUFSTaskCancelReason& OutReason)
{
	if (PendingEvents.IsEmpty())
	{
		return false;
	}

	const FTaskEvent Event = PendingEvents[0];
	PendingEvents.RemoveAt(0, 1, EAllowShrinking::No);
	OutFrom = Event.From;
	OutTo = Event.To;
	OutReason = Event.Reason;
	return true;
}

EYUFSActionTask UYUFSActionTaskComponent::MapActionToTask(EYUFSAction Action)
{
	switch (Action)
	{
	case EYUFSAction::Idle: return EYUFSActionTask::ContinueRoutine;
	case EYUFSAction::SeekInformation: return EYUFSActionTask::SeekInformation;
	case EYUFSAction::AlertNearbyOccupants: return EYUFSActionTask::AlertHelp;
	case EYUFSAction::GatherBelongings: return EYUFSActionTask::GatherBelongings;
	case EYUFSAction::HelpOther: return EYUFSActionTask::AssistOther;
	case EYUFSAction::WaitForInfo: return EYUFSActionTask::WaitForOfficialInfo;
	case EYUFSAction::Film: return EYUFSActionTask::FilmObserve;
	default: return EYUFSActionTask::None;
	}
}

float UYUFSActionTaskComponent::DrawDuration(
	EYUFSActionTask Task,
	FYUFSDeterministicRngSet& RandomSource) const
{
	switch (Task)
	{
	case EYUFSActionTask::SeekInformation:
		return DrawLogNormal(SeekInformationDuration, RandomSource);
	case EYUFSActionTask::GatherBelongings:
		return DrawLogNormal(GatherBelongingsDuration, RandomSource);
	case EYUFSActionTask::ContinueRoutine:
	case EYUFSActionTask::ObserveOthers:
	case EYUFSActionTask::WaitForOfficialInfo:
		return DrawLogNormal(WaitObserveDuration, RandomSource);
	case EYUFSActionTask::AssistOther:
	case EYUFSActionTask::AlertHelp:
		return DrawLogNormal(WarnHelpDuration, RandomSource);
	case EYUFSActionTask::InitialExtinguish:
		return DrawTriangular(SuppressTriangularSeconds, RandomSource);
	case EYUFSActionTask::Freeze:
		return RandomSource.FRandRange(EYUFSRngStream::TaskDuration, 1.f, 3.f);
	case EYUFSActionTask::FilmObserve:
	{
		const float MinDuration = FMath::Max(0.1f, FMath::Min(FilmDurationRange.X, FilmDurationRange.Y));
		const float MaxDuration = FMath::Max(MinDuration, FMath::Max(FilmDurationRange.X, FilmDurationRange.Y));
		return RandomSource.FRandRange(EYUFSRngStream::TaskDuration, MinDuration, MaxDuration);
	}
	default:
		return 3.f;
	}
}

float UYUFSActionTaskComponent::DrawLogNormal(
	const FYUFSLogNormalDurationModel& Model,
	FYUFSDeterministicRngSet& RandomSource)
{
	const float Median = FMath::Max(Model.MedianSeconds, 0.1f);
	const float Sigma = FMath::Max(Model.Sigma, 0.f);
	const float Cap = FMath::Max(Model.CapSeconds, 0.1f);
	const float U1 = FMath::Max(RandomSource.FRand(EYUFSRngStream::TaskDuration), UE_SMALL_NUMBER);
	const float U2 = RandomSource.FRand(EYUFSRngStream::TaskDuration);
	const float StandardNormal = FMath::Sqrt(-2.f * FMath::Loge(U1)) * FMath::Cos(2.f * PI * U2);
	return FMath::Clamp(FMath::Exp(FMath::Loge(Median) + Sigma * StandardNormal), 0.1f, Cap);
}

float UYUFSActionTaskComponent::DrawTriangular(
	const FVector& MinModeMax,
	FYUFSDeterministicRngSet& RandomSource)
{
	const float MinValue = FMath::Max(0.1f, static_cast<float>(MinModeMax.X));
	const float MaxValue = FMath::Max(MinValue, static_cast<float>(MinModeMax.Z));
	const float ModeValue = FMath::Clamp(static_cast<float>(MinModeMax.Y), MinValue, MaxValue);
	if (FMath::IsNearlyEqual(MinValue, MaxValue))
	{
		return MinValue;
	}

	const float Draw = RandomSource.FRand(EYUFSRngStream::TaskDuration);
	const float ModeFraction = (ModeValue - MinValue) / (MaxValue - MinValue);
	return Draw < ModeFraction
		? MinValue + FMath::Sqrt(Draw * (MaxValue - MinValue) * (ModeValue - MinValue))
		: MaxValue - FMath::Sqrt((1.f - Draw) * (MaxValue - MinValue) * (MaxValue - ModeValue));
}

void UYUFSActionTaskComponent::StartTask(
	EYUFSActionTask NewTask,
	FYUFSDeterministicRngSet& RandomSource)
{
	const EYUFSActionTask PreviousTask = CurrentTask;
	CurrentTask = NewTask;
	ElapsedSeconds = 0.f;
	PlannedDurationSeconds = DrawDuration(CurrentTask, RandomSource);
	PendingEvents.Add({ PreviousTask, CurrentTask, EYUFSTaskCancelReason::None });
}

void UYUFSActionTaskComponent::CancelTask(EYUFSTaskCancelReason Reason)
{
	if (CurrentTask == EYUFSActionTask::None)
	{
		return;
	}

	const EYUFSActionTask CancelledTask = CurrentTask;
	CurrentTask = EYUFSActionTask::None;
	ElapsedSeconds = 0.f;
	PlannedDurationSeconds = 0.f;
	PendingEvents.Add({ CancelledTask, CurrentTask, Reason });
}
