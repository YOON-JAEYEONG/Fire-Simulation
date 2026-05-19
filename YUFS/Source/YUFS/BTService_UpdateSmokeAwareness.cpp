// Fill out your copyright notice in the Description page of Project Settings.
//

#include "BTService_UpdateSmokeAwareness.h"

#include "SmokeAwareNPC.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Kismet/GameplayStatics.h"

#include "YUFSBinaryManager.h"
#include "YUFSHeterogeneousVolume.h"

UBTService_UpdateSmokeAwareness::UBTService_UpdateSmokeAwareness()
{
    NodeName = TEXT("Update Smoke Awareness");
    Interval = 0.5f;
    RandomDeviation = 0.1f;
}

void UBTService_UpdateSmokeAwareness::TickNode(
    UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory,
    float DeltaSeconds
)
{
    Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);
    UE_LOG(LogTemp, Warning, TEXT("[BTService] UpdateSmokeAwareness 실행됨"));    
    AAIController* AIController = OwnerComp.GetAIOwner();
    if (!AIController)
    {
        return;
    }

    APawn* Pawn = AIController->GetPawn();
    if (!Pawn)
    {
        return;
    }

    UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
    if (!Blackboard)
    {
        return;
    }

    AYUFSBinaryManager* FireManager = Cast<AYUFSBinaryManager>(
        UGameplayStatics::GetActorOfClass(Pawn->GetWorld(), AYUFSBinaryManager::StaticClass())
    );

    AYUFSHeterogeneousVolume* Volume = Cast<AYUFSHeterogeneousVolume>(
        UGameplayStatics::GetActorOfClass(Pawn->GetWorld(), AYUFSHeterogeneousVolume::StaticClass())
    );

    if (!FireManager || !Volume)
    {
        Blackboard->SetValueAsBool(TEXT("IsAwareOfFire"), false);
        return;
    }

    int32 CurrentFrame = Volume->GetFrame();

    uint8 SmokeDensity = 0;
    uint8 Temperature = 0;

    FVector CheckLocation = Pawn->GetActorLocation() + FVector(0, 0, 80.0f);

    bool bHasSmokeData = FireManager->GetSmokeDensityAtLocation(
        CheckLocation,
        CurrentFrame,
        SmokeDensity
    );

    bool bHasTemperatureData = FireManager->GetTemperatureAtLocation(
        CheckLocation,
        CurrentFrame,
        Temperature
    );

    if (!bHasSmokeData)
    {
        Blackboard->SetValueAsBool(TEXT("IsAwareOfFire"), false);
        Blackboard->SetValueAsInt(TEXT("SmokeDensity"), 0);
            return;
    }

    Blackboard->SetValueAsInt(TEXT("SmokeDensity"), SmokeDensity);

    if (bHasTemperatureData)
    {
        Blackboard->SetValueAsInt(TEXT("Temperature"), Temperature);
    }

    bool bCurrentlyAware = Blackboard->GetValueAsBool(TEXT("IsAwareOfFire"));

    bool bDetectedFireOrSmoke = SmokeDensity > 15 || Temperature > 40;

    // 한 번 인식하면 계속 true 유지
    bool bAware = bCurrentlyAware || bDetectedFireOrSmoke;

    Blackboard->SetValueAsBool(TEXT("IsAwareOfFire"), bAware);

    float CurrentFear = Blackboard->GetValueAsFloat(TEXT("Fear"));

    if (bAware)
    {
        CurrentFear += SmokeDensity * 0.02f;
        CurrentFear += Temperature * 0.01f;
    }
    else
    {
        CurrentFear -= 0.5f;
    }

    CurrentFear = FMath::Clamp(CurrentFear, 0.0f, 100.0f);

    ASmokeAwareNPC* SmokeNPC = Cast<ASmokeAwareNPC>(Pawn);

    if (SmokeNPC)
    {
        bool bInjured = SmokeNPC->bIsInjured;
        bool bNeedsHelp = SmokeNPC->bNeedsHelp;

        bool bCanHelpOthers =
            SmokeNPC->Altruism >= 0.7f &&
            !bInjured &&
            !bNeedsHelp &&
            bAware;
        Blackboard->SetValueAsBool(TEXT("CanHelpOthers"), bCanHelpOthers);
        // 연기 농도가 높으면 부상 처리
        if (SmokeDensity >= 20&&!bCanHelpOthers)
        {
            bInjured = true;
            bNeedsHelp = true;
        }

        SmokeNPC->bIsInjured = bInjured;
        SmokeNPC->bNeedsHelp = bNeedsHelp;

        Blackboard->SetValueAsBool(TEXT("IsInjured"), bInjured);
        Blackboard->SetValueAsBool(TEXT("NeedsHelp"), bNeedsHelp);

        

        Blackboard->SetValueAsInt(TEXT("ExitSelectMode"), SmokeNPC->ExitSelectMode);

        ACharacter* Character = Cast<ACharacter>(Pawn);
        if (Character && Character->GetCharacterMovement())
        {
            float NewSpeed = 250.0f;

            if (bInjured)
            {
                NewSpeed = 10.0f;
            }
            else if (CurrentFear >= 70.0f)
            {
                NewSpeed = 600.0f;
            }
            else if (bAware)
            {
                NewSpeed = 350.0f;
            }
            else
            {
                NewSpeed = 180.0f;
            }

            NewSpeed *= SmokeNPC->MoveAbility;

            Character->GetCharacterMovement()->MaxWalkSpeed = NewSpeed;
        }
    }
    Blackboard->SetValueAsFloat(TEXT("Fear"), CurrentFear); 
    Blackboard->SetValueAsBool(TEXT("IsPanic"), CurrentFear >= 70.0f);

    UE_LOG(LogTemp, Warning, TEXT("[BTService] Smoke=%d Temp=%d Fear=%.2f Aware=%d Panic=%d"),
        SmokeDensity,
        Temperature,
        CurrentFear,
        bAware,
        CurrentFear >= 70.0f
    );
}