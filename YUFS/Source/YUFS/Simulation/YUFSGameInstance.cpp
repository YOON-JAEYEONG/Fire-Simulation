// Fill out your copyright notice in the Description page of Project Settings.

#include "Simulation/YUFSGameInstance.h"

#include "Kismet/GameplayStatics.h"
#include "Simulation/YUFSRunResultsSaveGame.h"
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
#include "Simulation/YUFSAuthoredSuppressionScenario.h"

void UYUFSGameInstance::OnStart()
{
	Super::OnStart();
	SetupBuildingInteractions(GetWorld());
	if (FParse::Param(FCommandLine::Get(), TEXT("YUFSExtinguisherDemo")))
		UE_LOG(LogTemp, Warning, TEXT("[NPCSuppression] Legacy synthetic-fire demo disabled. Start only uses existing recorded volume playback; NPC targets require FDS ignition metadata."));
}

void UYUFSGameInstance::SetupBuildingInteractions(UWorld* World)
{
	if (!World || !World->IsGameWorld() || BuildingInteractionSetupWorld == World
		|| FParse::Param(FCommandLine::Get(), TEXT("YUFSNoBuildingInteractions"))
		|| (!bEnableBuildingInteractions
			&& !FParse::Param(FCommandLine::Get(), TEXT("YUFSBuildingInteractions")))) return;
	BuildingInteractionSetupWorld = World;
	const auto* Scenario = GetDefault<AYUFSAuthoredSuppressionScenario>();
	if (Scenario->bEnabled
		&& !FParse::Param(FCommandLine::Get(), TEXT("YUFSDisableAuthoredSuppression")))
	{
		bool bExists = false;
		for (TActorIterator<AYUFSAuthoredSuppressionScenario> It(World); It; ++It) { bExists = true; break; }
		if (!bExists) World->SpawnActor<AYUFSAuthoredSuppressionScenario>();
	}
	{
		// Add only environmental props after the building's own spawner distributes
		// its residents. Existing NPC identities, classes, traits and meshes remain.
		FTimerHandle Handle;
		const TWeakObjectPtr<UWorld> SetupWorld(World);
		const int32 TargetCount = FMath::Clamp(MinimumExtinguisherCount, 0, 64);
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [SetupWorld, TargetCount]()
		{
			UWorld* W = SetupWorld.Get();
			if (!W) return;
			auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(W);
			if (!Nav)
			{
				UE_LOG(LogTemp, Warning, TEXT("[NPCSuppression] Automatic prop placement unavailable: no navigation system. Existing props and ordinary simulation remain available."));
				return;
			}
			int32 Placed = 0;
			// Optional interaction-only population parameters; JJW's PADM personality,
			// movement, response timing and user-authored NPC placement remain untouched.
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
			bool bOverrideInteractionTraining = false;
			GConfig->GetBool(TEXT("YUFS.NpcInteraction"), TEXT("OverrideInteractionTrainingPopulation"), bOverrideInteractionTraining, GGameIni);
			GConfig->GetFloat(TEXT("YUFS.NpcInteraction"), TEXT("TrainedPopulationFraction"), TrainedFraction, GGameIni);
			GConfig->GetFloat(TEXT("YUFS.NpcInteraction"), TEXT("SuppressionChoiceProbability"), ChoiceProbability, GGameIni);
			for (TActorIterator<AYUFSEvacuationNPC> It(W); It; ++It)
			{
				FRandomStream PopulationRng(20260908 ^ It->GetStableNPCId());
				const bool bTrained = PopulationRng.FRand() < FMath::Clamp(TrainedFraction, 0.f, 1.f);
				if (bOverrideInteractionTraining && It->GetHumanCognitionComponent())
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
			int32 Existing = 0;
			for (TActorIterator<AYUFSFireExtinguisher> It(W); It; ++It)
			{
				++Existing;
				Anchors.Add(It->GetActorLocation());
			}
			if (bTargetConfigured && Existing < TargetCount)
			{
				FNavLocation NearRoom;
				if (Nav->ProjectPointToNavigation(KnownFire + FVector(250, 500, 0), NearRoom, FVector(100, 100, 80)))
				{
					FActorSpawnParameters Params;
					Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
					const FVector Location = NearRoom.Location + FVector(0, 0, 3);
					if (!Anchors.ContainsByPredicate([Location](const FVector& P)
						{ return FVector::DistSquared(P, Location) < FMath::Square(160.f); }))
						if (auto* Tool = W->SpawnActor<AYUFSFireExtinguisher>(Location, FRotator::ZeroRotator, Params))
						{ ++Placed; Anchors.Add(Tool->GetActorLocation()); }
				}
			}
			for (TActorIterator<AYUFSEvacuationNPC> It(W); It && Existing + Placed < TargetCount; ++It)
			{
				const FVector Origin = It->GetActorLocation();
				// Clustered residents must not collapse every prop into a single +X location.
				for (int32 Direction = 0; Direction < 8 && Existing + Placed < TargetCount; ++Direction)
				{
					const float Angle = Direction * PI / 4.f;
					const FVector Candidate = Origin + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * 180.f;
					FNavLocation ToolPoint;
					if (!Nav->ProjectPointToNavigation(Candidate, ToolPoint, FVector(60, 60, 140))
						|| FMath::Abs(ToolPoint.Location.Z - Origin.Z) > 150.f
						|| Anchors.ContainsByPredicate([&ToolPoint](const FVector& P)
							{ return FVector::DistSquared(P, ToolPoint.Location) < FMath::Square(160.f); })) continue;
					FCollisionQueryParams Sight(SCENE_QUERY_STAT(ExtinguisherPlacement), false, *It);
					if (W->LineTraceTestByChannel(Origin, ToolPoint.Location + FVector(0, 0, 70), ECC_Visibility, Sight)) continue;
					FActorSpawnParameters Params;
					Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
					auto* Tool = W->SpawnActor<AYUFSFireExtinguisher>(ToolPoint.Location + FVector(0, 0, 3), FRotator::ZeroRotator, Params);
					if (Tool)
					{
						Anchors.Add(ToolPoint.Location); ++Placed;
#if WITH_EDITOR
						Tool->SetActorLabel(FString::Printf(TEXT("Extinguisher_%02d"), Placed));
#endif
						UE_LOG(LogTemp, Display, TEXT("[NPCSuppression] Prop %s at %s"), *Tool->GetName(), *Tool->GetActorLocation().ToCompactString());
					}
				}
			}
			UE_LOG(LogTemp, Display, TEXT("[NPCSuppression] Placed %d extinguishers (%d already present, target %d); ZERO added fires and ZERO added NPCs. Recorded volume playback is unchanged by NPC attempts; only explicit FDS metadata supplies ignition targets."), Placed, Existing, TargetCount);
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
				// The ordinary GUI remains in WaitingToStart so JJW's palette can
				// place, rotate and delete NPCs. Auto-start is a separate opt-in for
				// unattended tests, never a side effect of adding interaction props.
				else if (FParse::Param(FCommandLine::Get(), TEXT("YUFSAutoStartSimulation")))
					It->StartSimulation();
			}
		}), 4.f, false);
	}
}

namespace
{
	const FString ResultsSaveSlotName = TEXT("YUFSRunResults");
	constexpr int32 ResultsSaveUserIndex = 0;
}

void UYUFSGameInstance::Init()
{
	Super::Init();
	LoadRunResultsFromDisk();
}

void UYUFSGameInstance::SaveRunResultsToDisk() const
{
	UYUFSRunResultsSaveGame* SaveGameObject = Cast<UYUFSRunResultsSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UYUFSRunResultsSaveGame::StaticClass()));
	if (!SaveGameObject)
	{
		return;
	}

	SaveGameObject->SavedResults = LastRunResults;
	UGameplayStatics::SaveGameToSlot(SaveGameObject, ResultsSaveSlotName, ResultsSaveUserIndex);
}

void UYUFSGameInstance::LoadRunResultsFromDisk()
{
	if (!UGameplayStatics::DoesSaveGameExist(ResultsSaveSlotName, ResultsSaveUserIndex))
	{
		return;
	}

	if (UYUFSRunResultsSaveGame* SaveGameObject = Cast<UYUFSRunResultsSaveGame>(
		UGameplayStatics::LoadGameFromSlot(ResultsSaveSlotName, ResultsSaveUserIndex)))
	{
		LastRunResults = SaveGameObject->SavedResults;
		UE_LOG(LogTemp, Log, TEXT("[YUFS] 저장된 회차 기록 %d건을 불러왔습니다."), LastRunResults.Num());
	}
}

void UYUFSGameInstance::LaunchScenario(const FYUFSScenarioConfig& Config)
{
	FText Error;
	if (!Config.IsValid(Error))
	{
		UE_LOG(LogTemp, Error, TEXT("[YUFS] LaunchScenario 거부: %s"), *Error.ToString());
		return;
	}

	ActiveScenario = Config;
	bHasActiveScenario = true;

	const FString PackageName = Config.SimulationMap.ToSoftObjectPath().GetLongPackageName();
	if (PackageName.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[YUFS] LaunchScenario: 맵 경로를 해석할 수 없습니다. (%s)"),
			*Config.SimulationMap.ToString());
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[YUFS] 시나리오 시작 → %s (NPC %d)"),
		*PackageName, Config.NPCCount);

	UGameplayStatics::OpenLevel(this, FName(*PackageName));
}

bool UYUFSGameInstance::RelaunchLastScenario()
{
	if (!bHasActiveScenario)
	{
		return false;
	}

	LaunchScenario(ActiveScenario);
	return true;
}

void UYUFSGameInstance::StoreRunResults(const TArray<FSimRunResult>& Results)
{
	// LastRunResults는 세션을 넘어 누적되는 전체 기록이므로 덮어쓰지 않고 이어붙입니다.
	LastRunResults.Append(Results);
	SaveRunResultsToDisk();
}

bool UYUFSGameInstance::ReplayRun(const FSimRunResult& RunResult)
{
	// "현재" ActiveScenario가 아니라 그 회차가 실제로 썼던 설정 스냅샷을 그대로 씁니다.
	// (그 사이 다른 시나리오를 실행했어도 이 회차는 정확히 그때 설정으로 재현됩니다.)
	FText Error;
	if (!RunResult.ScenarioConfig.IsValid(Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("[YUFS] ReplayRun 거부: 저장된 시나리오 설정이 유효하지 않습니다. %s"), *Error.ToString());
		return false;
	}

	// 배치 반복(1회로 제한)은 레벨 로드 후 SimulationController::StartSimulation()이 처리합니다.
	PendingReplaySeed = RunResult.RandomSeed;
	PendingReplayNPCTransforms = RunResult.InitialNPCTransforms;
	PendingReplayNPCClasses = RunResult.InitialNPCClasses;
	bHasPendingReplaySeed = true;

	LaunchScenario(RunResult.ScenarioConfig);
	return true;
}

void UYUFSGameInstance::ReturnToMainMenu()
{
	// ActiveScenario/LastRunResults는 "이어서 실행"과 결과 화면에서 쓸 수 있게 남겨둡니다.
	UE_LOG(LogTemp, Log, TEXT("[YUFS] 메인 메뉴로 복귀"));
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/Maps/Lvl_MainMenu")));
}
