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
#include "NPC/Integration/YUFSBelongingsRetrievalComponent.h"
#include "Simulation/YUFSSimulationController.h"
#include "Misc/ConfigCacheIni.h"
#include "Debug/YUFSInteractionPreview.h"

void UYUFSGameInstance::OnStart()
{
	Super::OnStart();
	SetupBuildingInteractions(GetWorld());
}

void UYUFSGameInstance::SetupBuildingInteractions(UWorld* World)
{
	if (!World || !World->IsGameWorld() || BuildingInteractionSetupWorld == World
		|| FParse::Param(FCommandLine::Get(), TEXT("YUFSNoBuildingInteractions"))
		|| (!bEnableBuildingInteractions
			&& !FParse::Param(FCommandLine::Get(), TEXT("YUFSBuildingInteractions")))) return;
	BuildingInteractionSetupWorld = World;
	{
		// Runs after the building's own spawner distributes its residents. Existing NPC
		// identities, classes, traits, meshes and placement remain untouched.
		FTimerHandle Handle;
		const TWeakObjectPtr<UWorld> SetupWorld(World);
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [SetupWorld]()
		{
			UWorld* W = SetupWorld.Get();
			if (!W) return;
			// Optional interaction-only population parameters; JJW's PADM personality,
			// movement, response timing and user-authored NPC placement remain untouched.
			float TrainedFraction = 0.65f, LeaveBehindProbability = -1.f;
			bool bOverrideInteractionTraining = false;
			GConfig->GetBool(TEXT("YUFS.NpcInteraction"), TEXT("OverrideInteractionTrainingPopulation"), bOverrideInteractionTraining, GGameIni);
			GConfig->GetFloat(TEXT("YUFS.NpcInteraction"), TEXT("TrainedPopulationFraction"), TrainedFraction, GGameIni);
			GConfig->GetFloat(TEXT("YUFS.NpcInteraction"), TEXT("BelongingsLeaveBehindProbability"), LeaveBehindProbability, GGameIni);
			for (TActorIterator<AYUFSEvacuationNPC> It(W); It; ++It)
			{
				FRandomStream PopulationRng(20260908 ^ It->GetStableNPCId());
				const bool bTrained = PopulationRng.FRand() < FMath::Clamp(TrainedFraction, 0.f, 1.f);
				if (bOverrideInteractionTraining && It->GetHumanCognitionComponent())
					It->GetHumanCognitionComponent()->Traits.FireTraining = bTrained ? 0.85f : 0.2f;
				if (LeaveBehindProbability >= 0.f && It->GetBelongingsRetrievalComponent())
					It->GetBelongingsRetrievalComponent()->LeaveBehindProbability = FMath::Clamp(LeaveBehindProbability, 0.f, 1.f);
			}
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
				// unattended tests, never a side effect of interaction setup.
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
