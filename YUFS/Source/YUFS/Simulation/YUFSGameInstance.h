// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "YUFSSimulationController.h"
#include "YUFSGameInstance.generated.h"

// 레벨 리로드를 넘어 배치 실험 상태를 보존하는 GameInstance
UCLASS()
class YUFS_API UYUFSGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	// 다음 레벨 시작 시 SimulationController가 읽어갈 배치 상태
	bool bHasPendingBatchRun = false;
	int32 PendingRunIndex = 1;
	int32 PendingTotalRuns = 1;
	TArray<FSimRunResult> AccumulatedResults;

	void SetupNextRun(int32 NextRunIndex, int32 TotalRuns, const TArray<FSimRunResult>& PreviousResults);
	void ClearBatchState();
};