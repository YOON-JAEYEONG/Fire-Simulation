// Fill out your copyright notice in the Description page of Project Settings.

#include "BTTask_FindRandomWanderLocation.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "NavigationSystem.h"

UBTTask_FindRandomWanderLocation::UBTTask_FindRandomWanderLocation()
{
    NodeName = TEXT("Find Random Wander Location");
}

EBTNodeResult::Type UBTTask_FindRandomWanderLocation::ExecuteTask(
    UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory
)
{
    AAIController* AIController = OwnerComp.GetAIOwner();

    if (!AIController)
    {
        UE_LOG(LogTemp, Warning, TEXT("[BTTask Wander] AIController 없음"));
        return EBTNodeResult::Failed;
    }

    APawn* Pawn = AIController->GetPawn();

    if (!Pawn)
    {
        UE_LOG(LogTemp, Warning, TEXT("[BTTask Wander] Pawn 없음"));
        return EBTNodeResult::Failed;
    }

    UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();

    if (!Blackboard)
    {
        UE_LOG(LogTemp, Warning, TEXT("[BTTask Wander] Blackboard 없음"));
        return EBTNodeResult::Failed;
    }

    UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(Pawn->GetWorld());

    if (!NavSystem)
    {
        UE_LOG(LogTemp, Warning, TEXT("[BTTask Wander] NavigationSystem 없음"));
        return EBTNodeResult::Failed;
    }

    FVector Origin = Pawn->GetActorLocation();
    FNavLocation RandomNavLocation;

    bool bFoundLocation = NavSystem->GetRandomReachablePointInRadius(
        Origin,
        WanderRadius,
        RandomNavLocation
    );

    if (!bFoundLocation)
    {
        UE_LOG(LogTemp, Warning, TEXT("[BTTask Wander] 랜덤 이동 위치를 찾지 못함"));
        return EBTNodeResult::Failed;
    }

    Blackboard->SetValueAsVector(
        WanderLocationKeyName,
        RandomNavLocation.Location
    );

    UE_LOG(LogTemp, Warning, TEXT("[BTTask Wander] WanderLocation 설정: %s"),
        *RandomNavLocation.Location.ToString()
    );

    return EBTNodeResult::Succeeded;
}