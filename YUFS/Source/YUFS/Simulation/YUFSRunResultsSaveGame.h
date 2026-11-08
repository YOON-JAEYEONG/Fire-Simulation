// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "YUFSSimulationController.h"
#include "YUFSRunResultsSaveGame.generated.h"

// 지금까지 실행한 모든 회차 결과(FSimRunResult, 각자 자기 Timestamp를 가짐)를 한 파일에
// 누적 저장하기 위한 SaveGame. 프로그램을 재시작해도 결과 화면에서 과거 기록을 볼 수 있게 합니다.
UCLASS()
class YUFS_API UYUFSRunResultsSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TArray<FSimRunResult> SavedResults;
};
