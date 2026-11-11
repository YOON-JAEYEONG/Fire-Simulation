#include "NPC/Animation/YUFSNPCAnimInstance.h"

#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "NPC/YUFSEvacuationNPC.h"

void UYUFSNPCAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!OwnerNPC)
	{
		OwnerNPC = Cast<AYUFSEvacuationNPC>(GetOwningActor());
		if (!OwnerNPC) return;
	}
	if (OwnerNPC->IsTimelinePlaybackMode())
	{
		const auto& Snapshot = OwnerNPC->GetTimelinePlaybackSnapshot();
		Speed = Snapshot.Velocity.Size2D();
		bIsMoving = Speed > 10.f;
		BehaviorState = Snapshot.BehaviorState;
		LastAction = Snapshot.DisplayAction;
		bIsCrawling = BehaviorState == EYUFSBehaviorState::Crawling;
		bIsIncapacitated = BehaviorState == EYUFSBehaviorState::Incapacitated;
		SmokeExposure = Snapshot.SmokeExposure;
		RiskPerception = Snapshot.RiskPerception;
		bIsCoughing = LastAction == EYUFSAction::Cough;
		bIsLookingAround = LastAction == EYUFSAction::SeekInformation;
		bIsFilming = LastAction == EYUFSAction::Film;
		return;
	}

	Speed = OwnerNPC->GetVelocity().Size2D();
	bIsMoving = Speed > 10.f;

	const UYUFSBehaviorStateMachine* BSM = OwnerNPC->GetBehaviorStateMachine();
	if (BSM)
	{
		BehaviorState   = BSM->GetCurrentState();
		bIsCrawling     = BSM->IsCrawling();
		bIsIncapacitated= BSM->IsIncapacitated();
		SmokeExposure   = BSM->GetSmokeExposure();
		RiskPerception  = BSM->GetRiskPerception();
	}

	LastAction      = OwnerNPC->GetLastAction();
	bIsCoughing     = (LastAction == EYUFSAction::Cough);
	bIsLookingAround= (LastAction == EYUFSAction::SeekInformation);
	bIsFilming      = (LastAction == EYUFSAction::Film);
}
