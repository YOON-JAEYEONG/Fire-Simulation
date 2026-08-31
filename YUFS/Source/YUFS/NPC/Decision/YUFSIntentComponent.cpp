#include "NPC/Decision/YUFSIntentComponent.h"

#include "Core/YUFSDeterministicRng.h"
#include "Core/YUFSObservation.h"
#include "NPC/Decision/YUFSBeliefComponent.h"

UYUFSIntentComponent::UYUFSIntentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UYUFSIntentComponent::UpdateIntent(
	float DeltaTime,
	const FYUFSNPCObservation& Observation,
	const UYUFSBeliefComponent& Belief,
	bool bHasSafeExit,
	FYUFSDeterministicRngSet& RandomSource)
{
	bIntentChanged = false;
	PreviousIntent = CurrentIntent;
	ReassessmentAccumulator += DeltaTime;

	if (Observation.CurrentState == EYUFSBehaviorState::Incapacitated)
	{
		SetIntent(EYUFSIntent::Incapacitated, TEXT("Incapacitated"));
		return;
	}

	if (Belief.HasImmediateLifeRisk())
	{
		SetIntent(bHasSafeExit ? EYUFSIntent::CommitEvac : EYUFSIntent::Shelter,
			bHasSafeExit ? TEXT("LifeRisk") : TEXT("LifeRiskNoSafeRoute"));
		return;
	}

	if (Belief.HasVerifiedOfficialInstruction())
	{
		SetIntent(bHasSafeExit ? EYUFSIntent::CommitEvac : EYUFSIntent::Shelter,
			bHasSafeExit ? TEXT("OfficialInstruction") : TEXT("OfficialInstructionNoSafeRoute"));
		return;
	}

	if (Observation.bNearbyNPCNeedsHelp)
	{
		SetIntent(EYUFSIntent::Help, TEXT("NearbyPersonNeedsHelp"));
		return;
	}

	if (CurrentIntent == EYUFSIntent::CommitEvac && bLockEvacuationCommit)
	{
		if (!bHasSafeExit)
		{
			SetIntent(EYUFSIntent::Shelter, TEXT("SafeRouteLost"));
		}
		return;
	}

	if (CurrentIntent == EYUFSIntent::Shelter && bHasSafeExit)
	{
		SetIntent(EYUFSIntent::CommitEvac, TEXT("SafeRouteRestored"));
		return;
	}

	// 5%의 무단서 기초값을 매초 다시 추첨하면 시간이 갈수록 거의 모두가
	// 대피하는 누적 편향이 생긴다. 실제 비상 cue가 없을 때는 관찰 상태를 유지한다.
	if (!Belief.HasEmergencyCue())
	{
		PreActionTargetCount = 0;
		PreActionCompletedCount = 0;
		bReappraisalRequested = false;
		SetIntent(EYUFSIntent::Observe, TEXT("NoEmergencyCue"));
		return;
	}

	const uint32 CueMask = Belief.GetActiveCueMask();
	const bool bCueChanged = CueMask != LastCueMask;
	if (!bCueChanged && !bReappraisalRequested)
	{
		return;
	}

	LastCueMask = CueMask;
	ReassessmentAccumulator = 0.f;
	bReappraisalRequested = false;

	// APPRAISE에서만 1회 추첨한다. 정기 tick은 재추첨하지 않고,
	// cue 변화 또는 대피 전 행동 완료가 다음 APPRAISE를 명시적으로 요청한다.
	if (RandomSource.Roll(EYUFSRngStream::Decision, Belief.GetCommitProbability()))
	{
		SetIntent(bHasSafeExit ? EYUFSIntent::CommitEvac : EYUFSIntent::Shelter,
			bHasSafeExit ? TEXT("BeliefCommit") : TEXT("BeliefCommitNoSafeRoute"));
	}
	else
	{
		if (PreActionTargetCount <= 0)
		{
			PreActionTargetCount = SamplePreActionTargetCount(RandomSource);
			PreActionCompletedCount = 0;
		}

		SetIntent(
			Belief.GetCommitProbability() >= PrepareProbabilityThreshold
				? EYUFSIntent::Prepare
				: EYUFSIntent::Observe,
			TEXT("PreActionRequired"));
	}
}

void UYUFSIntentComponent::NotifyPreActionCompleted(bool bHasSafeExit)
{
	if (PreActionTargetCount <= 0 || CurrentIntent == EYUFSIntent::CommitEvac
		|| CurrentIntent == EYUFSIntent::Shelter || CurrentIntent == EYUFSIntent::Incapacitated)
	{
		return;
	}

	PreActionCompletedCount = FMath::Min(PreActionCompletedCount + 1, PreActionTargetCount);
	if (PreActionCompletedCount >= PreActionTargetCount)
	{
		SetIntent(
			bHasSafeExit ? EYUFSIntent::CommitEvac : EYUFSIntent::Shelter,
			bHasSafeExit ? TEXT("PreActionTargetCompleted") : TEXT("PreActionTargetCompletedNoSafeRoute"));
		return;
	}

	bReappraisalRequested = true;
}

int32 UYUFSIntentComponent::SamplePreActionTargetCount(FYUFSDeterministicRngSet& RandomSource) const
{
	const float BandDraw = RandomSource.FRand(EYUFSRngStream::Decision);
	int32 MinCount = 1;
	int32 MaxCount = 5;
	if (BandDraw >= 0.966f)
	{
		MinCount = 10;
		MaxCount = 15;
	}
	else if (BandDraw >= 0.885f)
	{
		MinCount = 6;
		MaxCount = 9;
	}

	const int32 CountInBand = MaxCount - MinCount + 1;
	const int32 Offset = FMath::Min(
		FMath::FloorToInt(RandomSource.FRand(EYUFSRngStream::Decision) * CountInBand),
		CountInBand - 1);
	return MinCount + Offset;
}

void UYUFSIntentComponent::SetIntent(EYUFSIntent NewIntent, const TCHAR* Trigger)
{
	if (CurrentIntent == NewIntent)
	{
		return;
	}

	PreviousIntent = CurrentIntent;
	CurrentIntent = NewIntent;
	LastTrigger = Trigger;
	++DecisionIndex;
	bIntentChanged = true;
}
