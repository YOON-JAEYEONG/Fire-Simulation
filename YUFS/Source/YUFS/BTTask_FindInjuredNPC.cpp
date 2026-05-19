// Fill out your copyright notice in the Description page of Project Settings.

#include "BTTask_FindInjuredNPC.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Kismet/GameplayStatics.h"
#include "SmokeAwareNPC.h"

UBTTask_FindInjuredNPC::UBTTask_FindInjuredNPC()
{
    NodeName = TEXT("Find Injured NPC");
}

EBTNodeResult::Type UBTTask_FindInjuredNPC::ExecuteTask(
    UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory
)
{
    UE_LOG(LogTemp, Warning, TEXT("[BTTask] FindInjuredNPC 실행됨"));

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

    ASmokeAwareNPC* HelperNPC = Cast<ASmokeAwareNPC>(Pawn);
    if (!HelperNPC)
    {
        return EBTNodeResult::Failed;
    }

    UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
    if (!Blackboard)
    {
        return EBTNodeResult::Failed;
    }

    TArray<AActor*> NPCActors;
    UGameplayStatics::GetAllActorsOfClass(
        Pawn->GetWorld(),
        ASmokeAwareNPC::StaticClass(),
        NPCActors
    );

    ASmokeAwareNPC* ClosestInjuredNPC = nullptr;
    float ClosestDistance = TNumericLimits<float>::Max();

    for (AActor* Actor : NPCActors)
    {
        ASmokeAwareNPC* OtherNPC = Cast<ASmokeAwareNPC>(Actor);

        if (!OtherNPC || OtherNPC == HelperNPC)
        {
            continue;
        }

        if (!OtherNPC->bNeedsHelp)
        {
            continue;
        }

        float Distance = FVector::Dist(
            HelperNPC->GetActorLocation(),
            OtherNPC->GetActorLocation()
        );

        if (Distance > SearchRadius)
        {
            continue;
        }

        if (Distance < ClosestDistance)
        {
            ClosestDistance = Distance;
            ClosestInjuredNPC = OtherNPC;
        }
    }

    if (!ClosestInjuredNPC)
    {
        Blackboard->ClearValue(TargetNPCKeyName);
        Blackboard->ClearValue(TargetLocationKeyName);

        UE_LOG(LogTemp, Warning, TEXT("[BTTask] 도와줄 NPC 없음"));
        return EBTNodeResult::Failed;
    }

    Blackboard->SetValueAsObject(
        TargetNPCKeyName,
        ClosestInjuredNPC
    );

    Blackboard->SetValueAsVector(
        TargetLocationKeyName,
        ClosestInjuredNPC->GetActorLocation()
    );

    HelperNPC->bIsHelping = true;
    Blackboard->SetValueAsBool(TEXT("IsHelping"), true);

    UE_LOG(LogTemp, Warning, TEXT("[BTTask] 도와줄 NPC 발견: %s / 위치=%s"),
        *ClosestInjuredNPC->GetName(),
        *ClosestInjuredNPC->GetActorLocation().ToString()
    );

    return EBTNodeResult::Succeeded;
}