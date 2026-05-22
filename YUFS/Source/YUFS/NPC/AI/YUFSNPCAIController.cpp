#include "NPC/AI/YUFSNPCAIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"

AYUFSNPCAIController::AYUFSNPCAIController()
{
	bWantsPlayerState = false;
}

void AYUFSNPCAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (BehaviorTreeAsset)
	{
		RunBehaviorTree(BehaviorTreeAsset);
	}
}

void AYUFSNPCAIController::OnUnPossess()
{
	Super::OnUnPossess();
	if (BrainComponent)
	{
		BrainComponent->StopLogic(TEXT("Unpossessed"));
	}
}
