#include "FireEvacAIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"

AFireEvacAIController::AFireEvacAIController()
{
    UE_LOG(LogTemp, Warning, TEXT("[AIController] Constructor 실행"));
}

void AFireEvacAIController::BeginPlay()
{
    Super::BeginPlay();
}

void AFireEvacAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    UE_LOG(LogTemp, Warning, TEXT("FireEvacAIController OnPossess 실행됨"));

    if (BehaviorTreeAsset)
    {
        RunBehaviorTree(BehaviorTreeAsset);
        UE_LOG(LogTemp, Warning, TEXT("Behavior Tree 실행됨"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("BehaviorTreeAsset이 설정되지 않음"));
    }
}