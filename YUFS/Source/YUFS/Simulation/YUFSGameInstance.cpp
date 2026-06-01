// Fill out your copyright notice in the Description page of Project Settings.

#include "Simulation/YUFSGameInstance.h"

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