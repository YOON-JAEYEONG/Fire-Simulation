// Fill out your copyright notice in the Description page of Project Settings.

#include "Simulation/YUFSGameInstance.h"

#include "Kismet/GameplayStatics.h"
#include "Simulation/YUFSRunResultsSaveGame.h"

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
