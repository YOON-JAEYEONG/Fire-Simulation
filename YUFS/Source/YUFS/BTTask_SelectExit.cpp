// Fill out your copyright notice in the Description page of Project Settings.


#include "BTTask_SelectExit.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Kismet/GameplayStatics.h"
#include "SmokeAwareNPC.h"
#include "YUFSBinaryManager.h"
#include "YUFSHeterogeneousVolume.h"

UBTTask_SelectExit::UBTTask_SelectExit()
{
    NodeName = TEXT("Select Exit");
}

EBTNodeResult::Type UBTTask_SelectExit::ExecuteTask(
    UBehaviorTreeComponent& OwnerComp,
    uint8* NodeMemory
)
{
    UE_LOG(LogTemp, Warning, TEXT("[BTTask] SelectExit 실행됨"));

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
        ExitTag,
        ExitActors
    );

    if (ExitActors.Num() == 0)
    {
        UE_LOG(LogTemp, Error, TEXT("[BTTask] Exit 태그를 가진 출구가 없습니다."));
        return EBTNodeResult::Failed;
    }

    int32 ExitSelectMode = Blackboard->GetValueAsInt(ExitSelectModeKeyName);

    ASmokeAwareNPC* SmokeNPC = Cast<ASmokeAwareNPC>(Pawn);
    if (SmokeNPC)
    {
        ExitSelectMode = SmokeNPC->ExitSelectMode;
    }

    AActor* SelectedExit = nullptr;

    // 0: 가장 가까운 출구
    if (ExitSelectMode == 0)
    {
        float NearestDistance = TNumericLimits<float>::Max();
        FVector PawnLocation = Pawn->GetActorLocation();

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
                SelectedExit = ExitActor;
            }
        }
    }
    // 1: 랜덤 출구
    else if (ExitSelectMode == 1)
    {
        int32 RandomIndex = FMath::RandRange(0, ExitActors.Num() - 1);
        SelectedExit = ExitActors[RandomIndex];
    }
    // 2: 연기가 가장 적은 출구
    else if (ExitSelectMode == 2)
    {
        AYUFSBinaryManager* FireManager = Cast<AYUFSBinaryManager>(
            UGameplayStatics::GetActorOfClass(Pawn->GetWorld(), AYUFSBinaryManager::StaticClass())
        );

        AYUFSHeterogeneousVolume* Volume = Cast<AYUFSHeterogeneousVolume>(
            UGameplayStatics::GetActorOfClass(Pawn->GetWorld(), AYUFSHeterogeneousVolume::StaticClass())
        );

        uint8 LowestSmoke = 255;
        int32 CurrentFrame = Volume ? Volume->GetFrame() : 0;

        for (AActor* ExitActor : ExitActors)
        {
            if (!ExitActor)
            {
                continue;
            }

            uint8 ExitSmoke = 255;

            if (FireManager && Volume)
            {
                bool bHasSmoke = FireManager->GetSmokeDensityAtLocation(
                    ExitActor->GetActorLocation(),
                    CurrentFrame,
                    ExitSmoke
                );

                if (!bHasSmoke)
                {
                    ExitSmoke = 255;
                }
            }

            if (ExitSmoke < LowestSmoke)
            {
                LowestSmoke = ExitSmoke;
                SelectedExit = ExitActor;
            }
        }

        UE_LOG(LogTemp, Warning, TEXT("[BTTask] Safe Exit 선택 | Smoke=%d"), LowestSmoke);
    }

    if (!SelectedExit)
    {
        return EBTNodeResult::Failed;
    }

    Blackboard->SetValueAsVector(
        TargetLocationKeyName,
        SelectedExit->GetActorLocation()
    );

    UE_LOG(LogTemp, Warning, TEXT("[BTTask] 선택된 출구: %s"),
        *SelectedExit->GetName()
    );

    return EBTNodeResult::Succeeded;
}