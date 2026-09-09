#include "NPC/Decision/YUFSHumanBehaviorSelectorComponent.h"

#include "Core/YUFSDeterministicRng.h"
#include "Core/YUFSObservation.h"
#include "NPC/Cognition/YUFSBehaviorPolicy.h"
#include "NPC/Cognition/YUFSHumanCognitionTypes.h"

UYUFSHumanBehaviorSelectorComponent::UYUFSHumanBehaviorSelectorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

const FYUFSBehaviorDecision& UYUFSHumanBehaviorSelectorComponent::ResolveDecision(
	EYUFSAction ProposedLegacyAction,
	EYUFSIntent Intent,
	const FYUFSNPCObservation& Observation,
	const FYUFSCognitiveState& Cognition,
	const FYUFSHumanTraits& Traits,
	const FYUFSInteractionOpportunitySnapshot& Opportunities,
	bool bFreezeCue,
	FYUFSDeterministicRngSet& RandomSource)
{
	const bool bIntentChanged = Intent != LastIntent;
	const bool bKnowledgeChanged = Opportunities.KnowledgeRevision != LastKnowledgeRevision;
	const bool bSuppressionSafe = Traits.FireTraining >= (PolicyAsset ? PolicyAsset->MinimumSuppressionTraining : 0.55f)
		&& (Opportunities.bExtinguisherKnownAvailable || Opportunities.bHoldingExtinguisher)
		&& Opportunities.bSuppressibleFireKnown && Opportunities.bSafeRetreatKnown
		&& Cognition.PhysicalSeverity != EYUFSPerceivedPhysicalSeverity::ImmediateLifeThreat
		&& Cognition.PerceivedRisk < 0.65f
		&& !Observation.bReceivedLiveAnnouncement && !Observation.bReceivedStaffGuidance
		&& Observation.CurrentState != EYUFSBehaviorState::Incapacitated
		&& Observation.CurrentState != EYUFSBehaviorState::Crawling;
	// Acquiring the tool changes knowledge, but must not reroll an accepted task.
	if (CurrentDecision.Behavior == EYUFSHighLevelBehavior::AttemptSuppression && bSuppressionSafe
		&& PendingReselectionReason != FName(TEXT("TaskFinished"))
		&& Intent != EYUFSIntent::Incapacitated && Intent != EYUFSIntent::Shelter)
	{
		LastKnowledgeRevision = Opportunities.KnowledgeRevision;
		LastIntent = Intent;
		return CurrentDecision;
	}
	if (CurrentDecision.Behavior == EYUFSHighLevelBehavior::AttemptSuppression)
	{
		RequestReselection(TEXT("SuppressionNoLongerSafe"));
	}
	if ((Intent == EYUFSIntent::CommitEvac || ((bDemonstrateSuppressionWhenEligible || SuppressionProbabilityOverride >= 0.f)
		&& (Intent == EYUFSIntent::Observe || Intent == EYUFSIntent::Prepare))) && bSuppressionSafe)
	{
		// Changing to another nearby extinguisher is not another chance at the same fire.
		const FString Pair = Opportunities.FireStableId.ToString();
		if (!ConsideredSuppressionPairs.Contains(Pair))
		{
			ConsideredSuppressionPairs.Add(Pair);
			const float Base = PolicyAsset ? PolicyAsset->EvacuatingSuppressionProbability : 0.25f;
			const float Probability = SuppressionProbabilityOverride >= 0.f ? FMath::Clamp(SuppressionProbabilityOverride, 0.f, 1.f)
				: bDemonstrateSuppressionWhenEligible ? 1.f : FMath::Clamp(Base * (0.5f + Traits.FireTraining)
				* (0.5f + Traits.HelpingTendency) * (1.f - Cognition.PerceivedRisk), 0.f, 1.f);
			const bool bSelected = RandomSource.Roll(EYUFSRngStream::TaskChoice, Probability);
			UE_LOG(LogTemp, Display, TEXT("[NPCSuppressionChoice] %s probability=%.3f selected=%d training=%.2f risk=%.2f"),
				*GetNameSafe(GetOwner()), Probability, bSelected, Traits.FireTraining, Cognition.PerceivedRisk);
			if (bSelected)
			{
				CurrentDecision = FYUFSBehaviorDecision{};
				CurrentDecision.Revision = NextRevision++;
				CurrentDecision.Behavior = EYUFSHighLevelBehavior::AttemptSuppression;
				CurrentDecision.DesiredTask = EYUFSActionTask::InitialExtinguish;
				CurrentDecision.Reason = bDemonstrateSuppressionWhenEligible
					? TEXT("DemonstrationEligibleSuppression") : TEXT("EvacueeDiscoveredSuppressionOpportunity");
				LastIntent = Intent;
				LastKnowledgeRevision = Opportunities.KnowledgeRevision;
				bNeedsReselection = false;
				PendingReselectionReason = NAME_None;
				return CurrentDecision;
			}
		}
	}
	const bool bPrefire = !Observation.bAlarmSounding
		&& Observation.SmokeDensityAtSelf <= 0.f
		&& Observation.SmokeInFrontNormalized <= 0.f
		&& Observation.SmokeAboveNormalized <= 0.f
		&& !Observation.bReceivedLiveAnnouncement
		&& !Observation.bReceivedStaffGuidance
		&& !Opportunities.bSuppressibleFireKnown;

	if (bPrefire && Intent == EYUFSIntent::Observe)
	{
		FYUFSBehaviorDecision Prefire;
		Prefire.Behavior = EYUFSHighLevelBehavior::ContinueRoutine;
		Prefire.LegacyAction = EYUFSAction::Idle;
		Prefire.DesiredTask = EYUFSActionTask::ContinueRoutine;
		Prefire.Reason = TEXT("NoEmergencyCue");
		if (CurrentDecision.Behavior != Prefire.Behavior || bIntentChanged || bNeedsReselection)
		{
			Prefire.Revision = NextRevision++;
			CurrentDecision = Prefire;
		}
		LastIntent = Intent;
		LastKnowledgeRevision = Opportunities.KnowledgeRevision;
		bNeedsReselection = false;
		PendingReselectionReason = NAME_None;
		return CurrentDecision;
	}

	const bool bPreActionIntent = Intent == EYUFSIntent::Observe || Intent == EYUFSIntent::Prepare;
	if (bIntentChanged || bKnowledgeChanged || bNeedsReselection || CurrentDecision.Behavior == EYUFSHighLevelBehavior::None)
	{
		FYUFSBehaviorDecision Next = bPreActionIntent
			? SelectPreAction(Observation, Cognition, Traits, Opportunities, bFreezeCue, RandomSource)
			: BuildIntentDecision(ProposedLegacyAction, Intent);
		Next.Revision = NextRevision++;
		if (!PendingReselectionReason.IsNone())
		{
			Next.Reason = PendingReselectionReason;
		}
		CurrentDecision = Next;
		LastIntent = Intent;
		LastKnowledgeRevision = Opportunities.KnowledgeRevision;
		bNeedsReselection = false;
		PendingReselectionReason = NAME_None;
	}
	else if (!bPreActionIntent)
	{
		// Route policy may change its legacy route preference without changing intent.
		const FYUFSBehaviorDecision Next = BuildIntentDecision(ProposedLegacyAction, Intent);
		if (Next.Behavior != CurrentDecision.Behavior || Next.LegacyAction != CurrentDecision.LegacyAction)
		{
			CurrentDecision = Next;
			CurrentDecision.Revision = NextRevision++;
		}
	}

	return CurrentDecision;
}

void UYUFSHumanBehaviorSelectorComponent::NotifyTaskFinished(EYUFSActionTask FinishedTask)
{
	if (CurrentDecision.DesiredTask == FinishedTask)
	{
		LastCompletedBehavior = CurrentDecision.Behavior;
		RequestReselection(TEXT("TaskFinished"));
	}
}

void UYUFSHumanBehaviorSelectorComponent::RequestReselection(FName Reason)
{
	bNeedsReselection = true;
	PendingReselectionReason = Reason;
}

FYUFSBehaviorDecision UYUFSHumanBehaviorSelectorComponent::BuildIntentDecision(
	EYUFSAction ProposedLegacyAction,
	EYUFSIntent Intent) const
{
	FYUFSBehaviorDecision Result;
	Result.LegacyAction = ProposedLegacyAction;
	Result.Reason = TEXT("IntentProjection");

	switch (Intent)
	{
	case EYUFSIntent::CommitEvac:
		if (!IsLegacyNavigationAction(ProposedLegacyAction) || ProposedLegacyAction == EYUFSAction::HelpOther)
		{
			Result.LegacyAction = EYUFSAction::EvacuateToNearestExit;
		}
		if (Result.LegacyAction == EYUFSAction::EvacuateToFamiliarExit)
		{
			Result.Behavior = EYUFSHighLevelBehavior::EvacuateFamiliar;
		}
		else if (Result.LegacyAction == EYUFSAction::FollowCrowd)
		{
			Result.Behavior = EYUFSHighLevelBehavior::FollowCrowd;
		}
		else
		{
			Result.Behavior = EYUFSHighLevelBehavior::EvacuateNearest;
		}
		break;
	case EYUFSIntent::Help:
		Result.Behavior = EYUFSHighLevelBehavior::AssistOther;
		Result.LegacyAction = EYUFSAction::HelpOther;
		Result.DesiredTask = EYUFSActionTask::AssistOther;
		break;
	case EYUFSIntent::Shelter:
		Result.Behavior = EYUFSHighLevelBehavior::Shelter;
		Result.LegacyAction = EYUFSAction::WaitForInfo;
		break;
	case EYUFSIntent::Reenter:
		Result.Behavior = EYUFSHighLevelBehavior::Reenter;
		Result.LegacyAction = EYUFSAction::WaitForInfo;
		break;
	case EYUFSIntent::Incapacitated:
		Result.Behavior = EYUFSHighLevelBehavior::Incapacitated;
		Result.LegacyAction = EYUFSAction::Cough;
		break;
	default:
		Result.Behavior = EYUFSHighLevelBehavior::WaitObserve;
		Result.LegacyAction = EYUFSAction::WaitForInfo;
		Result.DesiredTask = EYUFSActionTask::ObserveOthers;
		break;
	}

	return Result;
}

FYUFSBehaviorDecision UYUFSHumanBehaviorSelectorComponent::SelectPreAction(
	const FYUFSNPCObservation& Observation,
	const FYUFSCognitiveState& Cognition,
	const FYUFSHumanTraits& Traits,
	const FYUFSInteractionOpportunitySnapshot& Opportunities,
	bool bFreezeCue,
	FYUFSDeterministicRngSet& RandomSource) const
{
	const UYUFSBehaviorPolicy* Policy = PolicyAsset;
	const float FreezeProbability = Policy ? Policy->FreezeBaseProbability : 0.08f;
	if (bFreezeCue && RandomSource.Roll(
		EYUFSRngStream::InteractionError,
		FreezeProbability * (0.5f + Traits.StressSensitivity)))
	{
		FYUFSBehaviorDecision Freeze;
		Freeze.Behavior = EYUFSHighLevelBehavior::Freeze;
		Freeze.LegacyAction = EYUFSAction::Idle;
		Freeze.DesiredTask = EYUFSActionTask::Freeze;
		Freeze.Reason = TEXT("AcuteStressFreeze");
		return Freeze;
	}

	const float SeekWeight = Policy ? Policy->SeekInformationWeight : 0.45f;
	const float WaitWeight = Policy ? Policy->WaitOrContinueWeight : 0.20f;
	const float BelongingsWeight = Policy ? Policy->RetrieveBelongingsWeight : 0.20f;
	const float WarnWeight = Policy ? Policy->WarnOrAssistWeight : 0.10f;
	const float SuppressWeight = Policy ? Policy->AttemptSuppressionWeight : 0.05f;
	const float RecordWeight = Policy ? Policy->ObserveOrRecordWeight : 0.03f;
	const float RepeatPenalty = Policy ? Policy->RepetitionPenalty : 0.45f;
	const bool bLegacyBelongings = Policy ? Policy->bUseLegacyBelongingsAssumption : true;
	const float MinSuppressionTraining = Policy ? Policy->MinimumSuppressionTraining : 0.55f;

	TArray<FCandidate, TInlineAllocator<8>> Candidates;
	auto AddCandidate = [this, &Candidates, RepeatPenalty](
		EYUFSHighLevelBehavior Behavior,
		EYUFSAction Action,
		EYUFSActionTask Task,
		float Weight,
		FName Reason)
	{
		const float RepetitionMultiplier = Behavior == LastCompletedBehavior ? RepeatPenalty : 1.f;
		const float FinalWeight = FMath::Max(0.f, Weight * RepetitionMultiplier);
		if (FinalWeight > UE_SMALL_NUMBER)
		{
			Candidates.Add({ Behavior, Action, Task, FinalWeight, Reason });
		}
	};

	const float Uncertainty = 1.f - Cognition.SituationConfidence;
	AddCandidate(
		EYUFSHighLevelBehavior::SeekInformation,
		EYUFSAction::SeekInformation,
		EYUFSActionTask::SeekInformation,
		SeekWeight * (0.65f + Uncertainty),
		TEXT("ReduceUncertainty"));

	const bool bAuthorityWaiting = Observation.bAlarmSounding
		&& !Observation.bReceivedLiveAnnouncement
		&& !Observation.bReceivedStaffGuidance
		&& Traits.AuthorityTrust >= 0.55f;
	const bool bContinueRoutine = Cognition.NormalcyBias >= 0.55f;
	AddCandidate(
		bContinueRoutine
			? EYUFSHighLevelBehavior::ContinueRoutine
			: (bAuthorityWaiting ? EYUFSHighLevelBehavior::WaitForAuthority : EYUFSHighLevelBehavior::WaitObserve),
		bContinueRoutine ? EYUFSAction::Idle : EYUFSAction::WaitForInfo,
		bContinueRoutine
			? EYUFSActionTask::ContinueRoutine
			: (bAuthorityWaiting ? EYUFSActionTask::WaitForOfficialInfo : EYUFSActionTask::ObserveOthers),
		WaitWeight * (0.60f + Cognition.NormalcyBias),
		bContinueRoutine ? TEXT("NormalcyBias") : (bAuthorityWaiting ? TEXT("AuthorityWait") : TEXT("ObserveOthers")));

	if ((Opportunities.bBelongingsKnown || bLegacyBelongings)
		&& Cognition.PhysicalSeverity != EYUFSPerceivedPhysicalSeverity::ImmediateLifeThreat)
	{
		AddCandidate(
			EYUFSHighLevelBehavior::RetrieveBelongings,
			EYUFSAction::GatherBelongings,
			EYUFSActionTask::GatherBelongings,
			BelongingsWeight * (1.f - Cognition.PerceivedRisk),
			Opportunities.bBelongingsKnown ? TEXT("KnownBelongings") : TEXT("LegacyBelongingsAssumption"));
	}

	if (Opportunities.bAssistPersonKnown)
	{
		AddCandidate(
			EYUFSHighLevelBehavior::AssistOther,
			EYUFSAction::HelpOther,
			EYUFSActionTask::AssistOther,
			WarnWeight * (0.50f + Traits.HelpingTendency),
			TEXT("NearbyPersonNeedsHelp"));
	}
	else if (Observation.NearbyNPCCount > 0
		&& (Observation.bReceivedPreRecordedMsg || Observation.bAlarmSounding))
	{
		AddCandidate(
			EYUFSHighLevelBehavior::WarnOthers,
			EYUFSAction::AlertNearbyOccupants,
			EYUFSActionTask::AlertHelp,
			WarnWeight * (0.50f + Traits.HelpingTendency),
			TEXT("WarnNearbyOccupants"));
	}

	const bool bRecordEligible = Observation.bAlarmSounding
		&& Cognition.PhysicalSeverity == EYUFSPerceivedPhysicalSeverity::AmbiguousAlarm
		&& !Observation.bReceivedLiveAnnouncement
		&& !Observation.bReceivedStaffGuidance;
	if (bRecordEligible)
	{
		AddCandidate(
			EYUFSHighLevelBehavior::ObserveOrRecord,
			EYUFSAction::Film,
			EYUFSActionTask::FilmObserve,
			RecordWeight * (1.f - Cognition.PerceivedRisk),
			TEXT("DistantObserver"));
	}

	const bool bSuppressionEligible = SuppressionProbabilityOverride < 0.f && Traits.FireTraining >= MinSuppressionTraining
		&& (Opportunities.bExtinguisherKnownAvailable || Opportunities.bHoldingExtinguisher)
		&& Opportunities.bSuppressibleFireKnown
		&& Opportunities.bSafeRetreatKnown
		&& Cognition.PhysicalSeverity != EYUFSPerceivedPhysicalSeverity::ImmediateLifeThreat
		&& Cognition.PerceivedRisk < 0.65f
		&& !Observation.bReceivedLiveAnnouncement && !Observation.bReceivedStaffGuidance;
	if (bSuppressionEligible)
	{
		AddCandidate(
			EYUFSHighLevelBehavior::AttemptSuppression,
			EYUFSAction::Idle,
			EYUFSActionTask::InitialExtinguish,
			SuppressWeight * (0.5f + Traits.FireTraining),
			TEXT("SuppressionSafetyGatePassed"));
	}

	if (Candidates.IsEmpty())
	{
		FYUFSBehaviorDecision Fallback;
		Fallback.Behavior = EYUFSHighLevelBehavior::WaitObserve;
		Fallback.LegacyAction = EYUFSAction::WaitForInfo;
		Fallback.DesiredTask = EYUFSActionTask::ObserveOthers;
		Fallback.Reason = TEXT("NoEligiblePreActionCandidate");
		return Fallback;
	}

	float TotalWeight = 0.f;
	for (const FCandidate& Candidate : Candidates)
	{
		TotalWeight += Candidate.Weight;
	}
	float Draw = RandomSource.FRand(EYUFSRngStream::TaskChoice) * TotalWeight;
	const FCandidate* Selected = &Candidates.Last();
	for (const FCandidate& Candidate : Candidates)
	{
		Draw -= Candidate.Weight;
		if (Draw <= 0.f)
		{
			Selected = &Candidate;
			break;
		}
	}

	FYUFSBehaviorDecision Result;
	Result.Behavior = Selected->Behavior;
	Result.LegacyAction = Selected->LegacyAction;
	Result.DesiredTask = Selected->Task;
	Result.Reason = Selected->Reason;
	return Result;
}

bool UYUFSHumanBehaviorSelectorComponent::IsLegacyNavigationAction(EYUFSAction Action)
{
	return Action == EYUFSAction::EvacuateToNearestExit
		|| Action == EYUFSAction::EvacuateToFamiliarExit
		|| Action == EYUFSAction::FollowCrowd
		|| Action == EYUFSAction::HelpOther;
}
