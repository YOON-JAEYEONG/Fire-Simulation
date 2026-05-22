#include "NPC/AI/YUFSBT_Task_MoveToShelter.h"
#include "NPC/AI/YUFSNPCAIController.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

UYUFSBT_Task_MoveToShelter::UYUFSBT_Task_MoveToShelter()
{
	NodeName = TEXT("Move To Shelter");
	bNotifyTick = true;
}

EBTNodeResult::Type UYUFSBT_Task_MoveToShelter::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AYUFSNPCAIController* Ctrl = Cast<AYUFSNPCAIController>(OwnerComp.GetAIOwner());
	AYUFSEvacuationNPC*   NPC  = Ctrl ? Cast<AYUFSEvacuationNPC>(Ctrl->GetPawn()) : nullptr;
	if (!NPC) return EBTNodeResult::Failed;

	if (UCharacterMovementComponent* Mv = NPC->GetCharacterMovement())
		if (Mv->MaxWalkSpeed < 1.f)
			Mv->MaxWalkSpeed = 400.f;

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	const FVector Target = BB ? BB->GetValueAsVector(YUFSBlackboardKeys::ShelterLoc) : FVector::ZeroVector;
	if (Target.IsZero()) return EBTNodeResult::Failed;

	if (UYUFSSmokeAwareNavigator* Nav = NPC->GetNavigator())
	{
		if (!Nav->bIsPathfinding)
		{
			Nav->ClearPath();
			Nav->RequestPathAsync(Target, NPC->GetCurrentSimFramePublic());
		}
	}

	return EBTNodeResult::InProgress;
}

void UYUFSBT_Task_MoveToShelter::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	AYUFSNPCAIController* Ctrl = Cast<AYUFSNPCAIController>(OwnerComp.GetAIOwner());
	AYUFSEvacuationNPC*   NPC  = Ctrl ? Cast<AYUFSEvacuationNPC>(Ctrl->GetPawn()) : nullptr;
	if (!NPC) { FinishLatentTask(OwnerComp, EBTNodeResult::Failed); return; }

	UYUFSSmokeAwareNavigator* Nav = NPC->GetNavigator();
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!Nav || !BB) { FinishLatentTask(OwnerComp, EBTNodeResult::Failed); return; }

	const FVector Target = BB->GetValueAsVector(YUFSBlackboardKeys::ShelterLoc);
	if (Target.IsZero()) { FinishLatentTask(OwnerComp, EBTNodeResult::Failed); return; }

	Nav->CheckAndReroute(NPC->GetCurrentSimFramePublic());

	if (!Nav->bIsPathfinding && FVector::Dist(Target, Nav->GetCurrentDestination()) > 500.f)
	{
		Nav->ClearPath();
		Nav->RequestPathAsync(Target, NPC->GetCurrentSimFramePublic());
	}

	if (Nav->bIsPathfinding || Nav->GetCurrentPathPoints().IsEmpty())
		return;

	NPC->DriveMovementToward(Target);

	if (FVector::Dist2D(NPC->GetActorLocation(), Target) < 150.f)
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
}

EBTNodeResult::Type UYUFSBT_Task_MoveToShelter::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	if (AYUFSNPCAIController* Ctrl = Cast<AYUFSNPCAIController>(OwnerComp.GetAIOwner()))
		if (AYUFSEvacuationNPC* NPC = Cast<AYUFSEvacuationNPC>(Ctrl->GetPawn()))
			if (UYUFSSmokeAwareNavigator* Nav = NPC->GetNavigator())
				Nav->ClearPath();
	return EBTNodeResult::Aborted;
}
