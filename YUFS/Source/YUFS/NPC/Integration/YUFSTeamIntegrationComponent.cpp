#include "NPC/Integration/YUFSTeamIntegrationComponent.h"

namespace
{
bool SameNavigationDirective(const FYUFSNavigationDirective& A, const FYUFSNavigationDirective& B)
{
	return A.StableNpcId == B.StableNpcId
		&& A.Goal == B.Goal
		&& A.DestinationHint.Equals(B.DestinationHint, 10.f)
		&& A.TargetStableId == B.TargetStableId
		&& A.bAllowHazardReroute == B.bAllowHazardReroute
		&& FMath::IsNearlyEqual(A.MaxPerceivedRisk, B.MaxPerceivedRisk, 0.01f)
		&& A.Reason == B.Reason;
}

bool SameMotionDirective(const FYUFSMotionDirective& A, const FYUFSMotionDirective& B)
{
	return A.StableNpcId == B.StableNpcId
		&& A.Semantic == B.Semantic
		&& A.LegacyAction == B.LegacyAction
		&& A.BehaviorState == B.BehaviorState
		&& FMath::IsNearlyEqual(A.SpeedScale, B.SpeedScale, 0.01f)
		&& A.bLoop == B.bLoop
		&& A.Reason == B.Reason;
}

bool SameInteractionDirective(const FYUFSInteractionDirective& A, const FYUFSInteractionDirective& B)
{
	return A.StableNpcId == B.StableNpcId
		&& A.Goal == B.Goal
		&& A.TargetStableId == B.TargetStableId
		&& A.TargetLocationHint.Equals(B.TargetLocationHint, 10.f)
		&& A.bRequiresReservation == B.bRequiresReservation
		&& FMath::IsNearlyEqual(A.MaxSearchRadiusCm, B.MaxSearchRadiusCm, 1.f)
		&& A.Reason == B.Reason;
}
}

UYUFSTeamIntegrationComponent::UYUFSTeamIntegrationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UYUFSTeamIntegrationComponent::PublishDecision(
	int32 StableNpcId,
	const FYUFSBehaviorDecision& Decision,
	EYUFSIntent Intent,
	EYUFSBehaviorState BehaviorState,
	const FVector& DestinationHint,
	bool bHasSafeExit,
	const FYUFSCognitiveState& Cognition)
{
	FYUFSNavigationDirective NextNavigation;
	NextNavigation.StableNpcId = StableNpcId;
	NextNavigation.Goal = MapNavigationGoal(Decision.Behavior);
	NextNavigation.DestinationHint = DestinationHint;
	NextNavigation.bAllowHazardReroute = true;
	NextNavigation.MaxPerceivedRisk = Intent == EYUFSIntent::Reenter
		? FMath::Clamp(0.35f + Cognition.PerceivedRisk * 0.20f, 0.f, 0.55f)
		: FMath::Clamp(0.65f - Cognition.Urgency * 0.35f, 0.20f, 0.80f);
	NextNavigation.Reason = Decision.Reason;
	if (!bHasSafeExit && Intent == EYUFSIntent::Shelter)
	{
		NextNavigation.Goal = EYUFSNavigationGoal::ShelterLocation;
	}
	if (NextNavigation.Goal == EYUFSNavigationGoal::InteractionTarget)
	{
		if (Decision.Behavior == EYUFSHighLevelBehavior::RetrieveBelongings)
		{
			NextNavigation.TargetStableId = InteractionOpportunities.BelongingsStableId;
			NextNavigation.DestinationHint = InteractionOpportunities.BelongingsLocation;
		}
		else
		{
			NextNavigation.TargetStableId = InteractionOpportunities.bHoldingExtinguisher
				? InteractionOpportunities.FireStableId
				: InteractionOpportunities.ExtinguisherStableId;
			NextNavigation.DestinationHint = InteractionOpportunities.bHoldingExtinguisher
				? InteractionOpportunities.FireLocation
				: InteractionOpportunities.ExtinguisherLocation;
		}
	}
	if (NextNavigation.Goal == EYUFSNavigationGoal::AssistTarget && InteractionOpportunities.bAssistPersonKnown)
	{
		NextNavigation.TargetStableId = InteractionOpportunities.AssistPersonStableId;
		NextNavigation.DestinationHint = InteractionOpportunities.AssistPersonLocation;
	}
	if (!SameNavigationDirective(NextNavigation, NavigationDirective))
	{
		NextNavigation.Revision = ++NavigationRevision;
		NavigationDirective = NextNavigation;
		OnNavigationDirectiveChanged.Broadcast(NavigationDirective);
	}

	FYUFSMotionDirective NextMotion;
	NextMotion.StableNpcId = StableNpcId;
	NextMotion.Semantic = MapMotionSemantic(Decision.Behavior, BehaviorState);
	NextMotion.LegacyAction = Decision.LegacyAction;
	NextMotion.BehaviorState = BehaviorState;
	NextMotion.SpeedScale = BehaviorState == EYUFSBehaviorState::Crawling
		? 0.35f
		: (Cognition.Urgency >= 0.70f ? 1.25f : 1.f);
	NextMotion.bLoop = Decision.Behavior != EYUFSHighLevelBehavior::Freeze;
	NextMotion.Reason = Decision.Reason;
	if (InteractionOpportunities.bDoorActionRequired && NextNavigation.Goal != EYUFSNavigationGoal::None)
	{
		NextMotion.Semantic = EYUFSMotionSemantic::OperateDoor;
		NextMotion.bLoop = false;
	}
	if (!SameMotionDirective(NextMotion, MotionDirective))
	{
		NextMotion.Revision = ++MotionRevision;
		MotionDirective = NextMotion;
		OnMotionDirectiveChanged.Broadcast(MotionDirective);
	}

	FYUFSInteractionDirective NextInteraction = BuildInteractionDirective(StableNpcId, Decision);
	if (!SameInteractionDirective(NextInteraction, InteractionDirective))
	{
		NextInteraction.Revision = ++InteractionRevision;
		InteractionDirective = NextInteraction;
		OnInteractionDirectiveChanged.Broadcast(InteractionDirective);
	}
}

void UYUFSTeamIntegrationComponent::SubmitInteractionOpportunities(
	const FYUFSInteractionOpportunitySnapshot& Snapshot)
{
	if (Snapshot.KnowledgeRevision < InteractionOpportunities.KnowledgeRevision)
	{
		return;
	}
	InteractionOpportunities = Snapshot;
}

void UYUFSTeamIntegrationComponent::SubmitNavigationFeedback(const FYUFSTeamRequestFeedback& Feedback)
{
	if (Feedback.RequestRevision != NavigationDirective.Revision)
	{
		return;
	}
	NavigationFeedback = Feedback;
	++FeedbackGeneration;
}

void UYUFSTeamIntegrationComponent::SubmitMotionFeedback(const FYUFSTeamRequestFeedback& Feedback)
{
	if (Feedback.RequestRevision != MotionDirective.Revision)
	{
		return;
	}
	MotionFeedback = Feedback;
	++FeedbackGeneration;
}

void UYUFSTeamIntegrationComponent::SubmitInteractionFeedback(const FYUFSTeamRequestFeedback& Feedback)
{
	if (Feedback.RequestRevision != InteractionDirective.Revision)
	{
		return;
	}
	InteractionFeedback = Feedback;
	++FeedbackGeneration;
}

EYUFSNavigationGoal UYUFSTeamIntegrationComponent::MapNavigationGoal(EYUFSHighLevelBehavior Behavior)
{
	switch (Behavior)
	{
	case EYUFSHighLevelBehavior::EvacuateNearest: return EYUFSNavigationGoal::SafeExit;
	case EYUFSHighLevelBehavior::EvacuateFamiliar: return EYUFSNavigationGoal::FamiliarExit;
	case EYUFSHighLevelBehavior::FollowCrowd: return EYUFSNavigationGoal::CrowdDestination;
	case EYUFSHighLevelBehavior::AssistOther: return EYUFSNavigationGoal::AssistTarget;
	case EYUFSHighLevelBehavior::SeekInformation: return EYUFSNavigationGoal::InvestigateTarget;
	case EYUFSHighLevelBehavior::RetrieveBelongings:
	case EYUFSHighLevelBehavior::AttemptSuppression:
		return EYUFSNavigationGoal::InteractionTarget;
	case EYUFSHighLevelBehavior::Shelter: return EYUFSNavigationGoal::ShelterLocation;
	case EYUFSHighLevelBehavior::Reenter: return EYUFSNavigationGoal::ReentryTarget;
	default: return EYUFSNavigationGoal::None;
	}
}

EYUFSMotionSemantic UYUFSTeamIntegrationComponent::MapMotionSemantic(
	EYUFSHighLevelBehavior Behavior,
	EYUFSBehaviorState BehaviorState)
{
	if (BehaviorState == EYUFSBehaviorState::Incapacitated) return EYUFSMotionSemantic::Cough;
	if (BehaviorState == EYUFSBehaviorState::Crawling) return EYUFSMotionSemantic::Crawl;
	switch (Behavior)
	{
	case EYUFSHighLevelBehavior::SeekInformation: return EYUFSMotionSemantic::LookAround;
	case EYUFSHighLevelBehavior::WaitObserve:
	case EYUFSHighLevelBehavior::WaitForAuthority:
	case EYUFSHighLevelBehavior::Shelter:
		return EYUFSMotionSemantic::Wait;
	case EYUFSHighLevelBehavior::RetrieveBelongings: return EYUFSMotionSemantic::GatherBelongings;
	case EYUFSHighLevelBehavior::WarnOthers: return EYUFSMotionSemantic::Warn;
	case EYUFSHighLevelBehavior::AssistOther: return EYUFSMotionSemantic::Assist;
	case EYUFSHighLevelBehavior::ObserveOrRecord: return EYUFSMotionSemantic::Record;
	case EYUFSHighLevelBehavior::AttemptSuppression: return EYUFSMotionSemantic::Extinguish;
	case EYUFSHighLevelBehavior::Freeze: return EYUFSMotionSemantic::Freeze;
	case EYUFSHighLevelBehavior::EvacuateNearest:
	case EYUFSHighLevelBehavior::EvacuateFamiliar:
	case EYUFSHighLevelBehavior::FollowCrowd:
	case EYUFSHighLevelBehavior::Reenter:
		return EYUFSMotionSemantic::Run;
	case EYUFSHighLevelBehavior::Incapacitated: return EYUFSMotionSemantic::Cough;
	default: return EYUFSMotionSemantic::Idle;
	}
}

FYUFSInteractionDirective UYUFSTeamIntegrationComponent::BuildInteractionDirective(
	int32 StableNpcId,
	const FYUFSBehaviorDecision& Decision) const
{
	FYUFSInteractionDirective Result;
	Result.StableNpcId = StableNpcId;
	Result.Reason = Decision.Reason;

	if (InteractionOpportunities.bDoorActionRequired
		&& MapNavigationGoal(Decision.Behavior) != EYUFSNavigationGoal::None)
	{
		Result.Goal = EYUFSInteractionGoal::OpenDoor;
		Result.TargetStableId = InteractionOpportunities.DoorStableId;
		Result.TargetLocationHint = InteractionOpportunities.DoorUseLocation;
		Result.bRequiresReservation = true;
		return Result;
	}

	switch (Decision.Behavior)
	{
	case EYUFSHighLevelBehavior::SeekInformation:
		Result.Goal = EYUFSInteractionGoal::InspectHazard;
		break;
	case EYUFSHighLevelBehavior::RetrieveBelongings:
		Result.Goal = EYUFSInteractionGoal::RetrieveBelongings;
		Result.TargetStableId = InteractionOpportunities.BelongingsStableId;
		Result.TargetLocationHint = InteractionOpportunities.BelongingsLocation;
		Result.bRequiresReservation = true;
		break;
	case EYUFSHighLevelBehavior::AttemptSuppression:
		Result.Goal = InteractionOpportunities.bHoldingExtinguisher
			? EYUFSInteractionGoal::SuppressFire
			: EYUFSInteractionGoal::AcquireExtinguisher;
		Result.TargetStableId = InteractionOpportunities.bHoldingExtinguisher
			? InteractionOpportunities.FireStableId
			: InteractionOpportunities.ExtinguisherStableId;
		Result.TargetLocationHint = InteractionOpportunities.bHoldingExtinguisher
			? InteractionOpportunities.FireLocation
			: InteractionOpportunities.ExtinguisherLocation;
		Result.bRequiresReservation = true;
		break;
	case EYUFSHighLevelBehavior::AssistOther:
		Result.Goal = EYUFSInteractionGoal::AssistPerson;
		Result.TargetStableId = InteractionOpportunities.AssistPersonStableId;
		Result.TargetLocationHint = InteractionOpportunities.AssistPersonLocation;
		Result.bRequiresReservation = true;
		break;
	default:
		break;
	}

	return Result;
}
