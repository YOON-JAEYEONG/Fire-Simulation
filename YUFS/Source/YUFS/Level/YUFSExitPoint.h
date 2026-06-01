// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "YUFSExitPoint.generated.h"

UCLASS()
class YUFS_API AYUFSExitPoint : public AActor
{
	GENERATED_BODY()
	
public:
	AYUFSExitPoint();

public:
	// 에디터에서 각 출구마다 설정
	UPROPERTY(EditAnywhere, Category="Exit")
	FName ExitID;           // "MainEntrance", "EmergencyExit_B2" 등

	UPROPERTY(EditAnywhere, Category="Exit")
	float ExitWidth = 150.f; // 출구 폭 (혼잡도 계산용)

	UPROPERTY(EditAnywhere, Category="Exit")
	bool bIsFamiliarEntry = false; // NPC 진입 시 사용한 출구인지

};
