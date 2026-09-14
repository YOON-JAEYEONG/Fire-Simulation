// Fill out your copyright notice in the Description page of Project Settings.

#include "Simulation/YUFSGameInstance.h"

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
		UE_LOG(LogTemp, Warning, TEXT("[NPCSuppression] Legacy synthetic-fire demo disabled. Start only uses existing recorded volume playback; NPC targets require FDS ignition metadata."));
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
			// Metadata readiness and activation are separate: a valid future ignition is not a setup error.
			// Never advise restoring obsolete manually marked local targets or spawn a fallback fire.
			FVector KnownFire = FVector::ZeroVector;
			bool bTargetConfigured = false;
			bool bHasFireVolume = false;
			bool bHasMetadata = false;
			for (TActorIterator<AYUFSHeterogeneousVolume> Fire(W); Fire; ++Fire)
			{
				bHasFireVolume = true;
				if (!Fire->HasFdsIgnitionMetadata())
				{
					UE_LOG(LogTemp, Warning, TEXT("[FDSIgnition] %s has no accepted ignition metadata (%s). Supply source-derived coordinates and confirmed mapping; no local-coordinate fallback is used."),
						*Fire->GetName(), *Fire->GetFdsTargetDiagnostic());
					continue;
				}
				bHasMetadata = true;
				if (!bTargetConfigured && Fire->GetInteractionTarget(KnownFire)) bTargetConfigured = true;
			}
			if (!bHasFireVolume)
			{
				UE_LOG(LogTemp, Warning, TEXT("[FDSIgnition] No existing recorded fire volume is present. No fire actor or guessed target will be created."));
			}
			else if (!bHasMetadata)
			{
				UE_LOG(LogTemp, Warning, TEXT("[NPCSuppression] FDS ignition source unavailable: suppression attempts remain unavailable. Existing recorded playback and evacuation are not replaced by a demo fire."));
			}
			else if (!bTargetConfigured)
			{
				UE_LOG(LogTemp, Display, TEXT("[FDSIgnition] Metadata contract accepted; waiting for its explicit activation time and simulation clock. This is not a missing-target configuration error."));
			}
			if (bHasMetadata)
			{
				UE_LOG(LogTemp, Display, TEXT("[NPCSuppression] Ignition metadata does not authorize sensor data: binary sampling independently requires bDatasetAlignmentConfirmed and a fully loaded valid frame."));
			}
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
			UE_LOG(LogTemp, Display, TEXT("[NPCSuppression] Placed %d extinguishers; ZERO added fires and ZERO added NPCs. Recorded volume playback is unchanged by NPC attempts; only explicit FDS metadata supplies ignition targets."), Placed);
			const bool bPreviewMode = FParse::Param(FCommandLine::Get(), TEXT("YUFSInteractionPreview"));
			AYUFSInteractionPreview* Preview = bPreviewMode ? W->SpawnActor<AYUFSInteractionPreview>() : nullptr;
			for (TActorIterator<AYUFSSimulationController> It(W); It; ++It)
			{
				if (bPreviewMode)
				{
					It->MaxSimDurationSeconds=3600.f;
					It->bEnableTimelineRecording=false;
				}
				// Preserve the configured countdown; do not shift FDS events to accelerate a demo.
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
