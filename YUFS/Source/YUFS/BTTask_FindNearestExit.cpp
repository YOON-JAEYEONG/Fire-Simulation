// Fill out your copyright notice in the Description page of Project Settings.


#include "BTTask_FindNearestExit.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/TargetPoint.h"

UBTTask_FindNearestExit::UBTTask_FindNearestExit()
{
    NodeName = TEXT("Find Nearest Exit");
}

EBTNodeResult::Type UBTTask_FindNearestExit::ExecuteTask(
    UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory
)
{
    UE_LOG(LogTemp, Warning, TEXT("[BTTask] FindNearestExit 실행됨"));
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

    TArray<AActor*> ExitActors;
    UGameplayStatics::GetAllActorsWithTag(
        Pawn->GetWorld(),
        FName("Exit"),
        ExitActors
    );

    if (ExitActors.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("Exit 태그를 가진 출구가 없습니다."));
        return EBTNodeResult::Failed;
    }

    AActor* NearestExit = nullptr;
    float NearestDistance = TNumericLimits<float>::Max();

    FVector PawnLocation = Pawn->GetActorLocation();
    UE_LOG(LogTemp, Warning, TEXT("[BTTask] Exit 개수 = %d"), ExitActors.Num());
    for (AActor* ExitActor : ExitActors)
    {
        if (!ExitActor)
        {
            continue;
        }

        float Distance = FVector::Dist(PawnLocation, ExitActor->GetActorLocation());

        if (Distance < NearestDistance)
        {
            NearestDistance = Distance;
            NearestExit = ExitActor;
        }
    }

    if (!NearestExit)
    {
        return EBTNodeResult::Failed;
    }

    Blackboard->SetValueAsVector(
        TEXT("TargetLocation"),
        NearestExit->GetActorLocation()
    );

    Blackboard->SetValueAsBool(TEXT("HasTarget"), true);

    UE_LOG(LogTemp, Warning, TEXT("가장 가까운 출구 설정: %s"),
        *NearestExit->GetName()
    );

    return EBTNodeResult::Succeeded;
}