// Fill out your copyright notice in the Description page of Project Settings.
#include "BTTask_FindPanicLocation.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "NavigationSystem.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

UBTTask_FindPanicLocation::UBTTask_FindPanicLocation()
{
    NodeName = TEXT("Find Panic Location");
}

EBTNodeResult::Type UBTTask_FindPanicLocation::ExecuteTask(
    UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory
)
{
    UE_LOG(LogTemp, Warning, TEXT("[BTTask] FindPanicLocation 실행됨"));

    AAIController* AIController = OwnerComp.GetAIOwner();
    if (!AIController)
    {
        return EBTNodeResult::Failed;
    }

    APawn* Pawn = AIController->GetPawn();
    if (!Pawn)
    {
        return EBTNodeResult::Failed;
    }

    UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
    if (!Blackboard)
    {
        return EBTNodeResult::Failed;
    }

    ACharacter* Character = Cast<ACharacter>(Pawn);
    if (Character && Character->GetCharacterMovement())
    {
        Character->GetCharacterMovement()->MaxWalkSpeed = 600.0f;
    }

    UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(Pawn->GetWorld());
    if (!NavSystem)
    {
        return EBTNodeResult::Failed;
    }

    FNavLocation RandomLocation;
    bool bFound = NavSystem->GetRandomReachablePointInRadius(
        Pawn->GetActorLocation(),
        PanicMoveRadius,
        RandomLocation
    );

    if (!bFound)
    {
        UE_LOG(LogTemp, Warning, TEXT("[BTTask] 패닉 이동 위치를 찾지 못함"));
        return EBTNodeResult::Failed;
    }

    Blackboard->SetValueAsVector(
        TargetLocationKeyName,
        RandomLocation.Location
    );

    UE_LOG(LogTemp, Warning, TEXT("[BTTask] Panic TargetLocation=%s"),
        *RandomLocation.Location.ToString()
    );

    return EBTNodeResult::Succeeded;
}