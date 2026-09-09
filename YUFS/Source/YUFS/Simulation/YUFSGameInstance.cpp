// Fill out your copyright notice in the Description page of Project Settings.

#include "Simulation/YUFSGameInstance.h"

#include "Debug/YUFSExtinguisherDemoDirector.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Cognition/YUFSHumanCognitionComponent.h"
#include "Components/CapsuleComponent.h"
#include "Fire/YUFSFireExtinguisher.h"
#include "Fire/YUFSHeterogeneousVolume.h"
#include "NPC/Integration/YUFSNpcSuppressionComponent.h"
#include "NPC/Decision/YUFSHumanBehaviorSelectorComponent.h"
#include "Simulation/YUFSSimulationController.h"
#include "NavigationSystem.h"
#include "Misc/ConfigCacheIni.h"
#include "Debug/YUFSInteractionPreview.h"

void UYUFSGameInstance::OnStart()
{
	Super::OnStart();
	SetupBuildingInteractions(GetWorld());
	if (FParse::Param(FCommandLine::Get(), TEXT("YUFSExtinguisherDemo")))
		UE_LOG(LogTemp, Warning, TEXT("[NPCSuppression] Legacy synthetic-fire demo disabled. Use the existing level fire, not spawned fire props."));
}

void UYUFSGameInstance::SetupBuildingInteractions(UWorld* World)
{
	if (!World || BuildingInteractionSetupWorld == World
		|| !FParse::Param(FCommandLine::Get(), TEXT("YUFSBuildingInteractions"))) return;
	BuildingInteractionSetupWorld = World;
	{
		// Add only environmental props after the building's own spawner distributes
		// its residents. Existing NPC identities, classes, traits and meshes remain.
		FTimerHandle Handle;
		const TWeakObjectPtr<UWorld> SetupWorld(World);
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [SetupWorld]()
		{
			UWorld* W = SetupWorld.Get();
			if (!W) return;
			auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(W);
			if (!Nav)
			{
				UE_LOG(LogTemp, Error, TEXT("[InteractionPreview] Setup failed: no navigation system. Start remains blocked in preview mode."));
				return;
			}
			int32 Placed = 0;
			// Configurable training population and independent choice probability; no selected IDs.
			// No fire is synthesized when the level's ignition coordinate is missing.
			FVector KnownFire;
			bool bTargetConfigured = false;
			for (TActorIterator<AYUFSHeterogeneousVolume> Fire(W); Fire; ++Fire)
				if (Fire->GetInteractionTarget(KnownFire)) { bTargetConfigured = true; break; }
			if (!bTargetConfigured)
				UE_LOG(LogTemp, Error, TEXT("[NPCSuppression] Set InteractionTargetLocal on the EXISTING fire volume. No guessed ignition point will be used."));
			float TrainedFraction = 0.65f, ChoiceProbability = 0.35f;
			GConfig->GetFloat(TEXT("YUFS.NpcInteraction"), TEXT("TrainedPopulationFraction"), TrainedFraction, GGameIni);
			GConfig->GetFloat(TEXT("YUFS.NpcInteraction"), TEXT("SuppressionChoiceProbability"), ChoiceProbability, GGameIni);
			for (TActorIterator<AYUFSEvacuationNPC> It(W); It; ++It)
			{
				FRandomStream PopulationRng(20260908 ^ It->GetStableNPCId());
				const bool bTrained = PopulationRng.FRand() < FMath::Clamp(TrainedFraction, 0.f, 1.f);
				if (It->GetHumanCognitionComponent())
					It->GetHumanCognitionComponent()->Traits.FireTraining = bTrained ? 0.85f : 0.2f;
				if (It->GetHumanBehaviorSelector())
				{
					It->GetHumanBehaviorSelector()->bDemonstrateSuppressionWhenEligible = false;
					It->GetHumanBehaviorSelector()->SuppressionProbabilityOverride = FMath::Clamp(ChoiceProbability, 0.f, 1.f);
				}
				if (It->GetSuppressionComponent())
					It->GetSuppressionComponent()->bKnowsScenarioFireLocation = false;
			}
			TArray<FVector> Anchors;
			if (bTargetConfigured)
			{
				FNavLocation NearRoom;
				if (Nav->ProjectPointToNavigation(KnownFire + FVector(250, 500, 0), NearRoom, FVector(100, 100, 80)))
				{
					FActorSpawnParameters Params;
					Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
					if (W->SpawnActor<AYUFSFireExtinguisher>(NearRoom.Location + FVector(0, 0, 3), FRotator::ZeroRotator, Params)) ++Placed;
				}
			}
			for (TActorIterator<AYUFSEvacuationNPC> It(W); It && Placed < 4; ++It)
			{
				const FVector Origin = It->GetActorLocation();
				if (Anchors.ContainsByPredicate([Origin](const FVector& P) { return FVector::DistSquared(P, Origin) < FMath::Square(650.f); })) continue;
				FNavLocation ToolPoint;
				if (!Nav->ProjectPointToNavigation(Origin + FVector(150, 0, 0), ToolPoint, FVector(100, 100, 140))) continue;
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
				auto* Tool = W->SpawnActor<AYUFSFireExtinguisher>(ToolPoint.Location + FVector(0, 0, 3), FRotator::ZeroRotator, Params);
				if (Tool) { Anchors.Add(Origin); ++Placed; }
			}
			UE_LOG(LogTemp, Display, TEXT("[NPCSuppression] Placed %d extinguishers; ZERO added fires and ZERO added NPCs. Existing level fire is authoritative."), Placed);
			const bool bPreviewMode = FParse::Param(FCommandLine::Get(), TEXT("YUFSInteractionPreview"));
			AYUFSInteractionPreview* Preview = bPreviewMode ? W->SpawnActor<AYUFSInteractionPreview>() : nullptr;
			for (TActorIterator<AYUFSSimulationController> It(W); It; ++It)
			{
				if (bPreviewMode)
				{
					It->MaxSimDurationSeconds=3600.f;
					It->bEnableTimelineRecording=false;
				}
				It->FireStartDelaySeconds = 1.f;
				if (bPreviewMode)
					It->NotifyInteractionPreviewReady(Preview && Preview->IsReady());
				else
					It->StartSimulation();
			}
		}), 4.f, false);
	}
}

void UYUFSGameInstance::SetupNextRun(int32 NextRunIndex, int32 TotalRuns, const TArray<FSimRunResult>& PreviousResults)
{
	bHasPendingBatchRun = true;
	PendingRunIndex = NextRunIndex;
	PendingTotalRuns = TotalRuns;
	AccumulatedResults = PreviousResults;
}

void UYUFSGameInstance::ClearBatchState()
{
	bHasPendingBatchRun = false;
	PendingRunIndex = 1;
	PendingTotalRuns = 1;
	AccumulatedResults.Empty();
}
