// Fill out your copyright notice in the Description page of Project Settings.

#include "BTTask_HelpNPC.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "SmokeAwareNPC.h"
#include "GameFramework/CharacterMovementComponent.h"

UBTTask_HelpNPC::UBTTask_HelpNPC()
{
    NodeName = TEXT("Help NPC");
}

EBTNodeResult::Type UBTTask_HelpNPC::ExecuteTask(
    UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory
)
{
    UE_LOG(LogTemp, Warning, TEXT("[BTTask] HelpNPC 실행됨"));

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

    UObject* TargetObject = Blackboard->GetValueAsObject(TargetNPCKeyName);
    ASmokeAwareNPC* TargetNPC = Cast<ASmokeAwareNPC>(TargetObject);

    if (!TargetNPC)
    {
        HelperNPC->bIsHelping = false;
        Blackboard->SetValueAsBool(TEXT("IsHelping"), false);
        Blackboard->ClearValue(TargetNPCKeyName);
        return EBTNodeResult::Failed;
    }

    float Distance = FVector::Dist(
        HelperNPC->GetActorLocation(),
        TargetNPC->GetActorLocation()
    );

    if (Distance > HelpDistance)
    {
        UE_LOG(LogTemp, Warning, TEXT("[BTTask] HelpNPC 거리 부족: %.2f"), Distance);
        return EBTNodeResult::Failed;
    }

    TargetNPC->bNeedsHelp = false;
    TargetNPC->bIsInjured = false;

    if (TargetNPC->GetCharacterMovement())
    {
        TargetNPC->GetCharacterMovement()->MaxWalkSpeed = 220.0f;
    }

    HelperNPC->bIsHelping = false;

    Blackboard->SetValueAsBool(TEXT("IsHelping"), false);
    Blackboard->ClearValue(TargetNPCKeyName);

    UE_LOG(LogTemp, Warning, TEXT("[BTTask] NPC 도움 완료: %s"),
        *TargetNPC->GetName()
    );

    return EBTNodeResult::Succeeded;
}