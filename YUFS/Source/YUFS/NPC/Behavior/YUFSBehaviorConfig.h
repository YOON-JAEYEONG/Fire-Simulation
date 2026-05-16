// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "YUFSBehaviorConfig.generated.h"

/**
 * 
 */
UCLASS()
class YUFS_API UYUFSBehaviorConfig : public UDataAsset
{
	GENERATED_BODY()
	
public:
	UPROPERTY(EditAnywhere)
	float MaxMillingDuration = 30.f;
	
	UPROPERTY(EditAnywhere)
	float PreparationDuration = 5.f;
	
	UPROPERTY(EditAnywhere)
	float RiskPerceptionThreshold = 0.40f;

	float RiskAccumSpeed = 0.05f;
	
	UPROPERTY(EditAnywhere)
	float SmokeAwarenessThreshold = 0.15f;

	UPROPERTY(EditAnywhere)
	float VisionSmokeCueThreshold = 0.15f;

	UPROPERTY(EditAnywhere)
	float VisionRiskAccumMultiplier = 0.75f;

	UPROPERTY(EditAnywhere)
	float AboveSmokeCueThreshold = 0.15f;

	UPROPERTY(EditAnywhere)
	float AboveSmokeRiskAccumMultiplier = 0.5f;

	UPROPERTY(EditAnywhere)
	float LookAroundYawAmplitudeDegrees = 45.f;

	UPROPERTY(EditAnywhere)
	float LookAroundFrequencyHz = 0.6f;
	
	UPROPERTY(EditAnywhere)
	float EmergencyOverrideMultiplier = 2.0f;

	// ── 행동 불능 (Incapacitation) ─────────────────────────────────────
	// 연기 흡입량 누적 속도 (단위: /초, 1.0 이면 100초에 완전 행동불능)
	UPROPERTY(EditAnywhere, Category="Incapacitation")
	float SmokeExposureAccumRate = 0.008f;

	// 행동불능 전 '기어가기' 단계 진입 임계값 [0,1]
	UPROPERTY(EditAnywhere, Category="Incapacitation")
	float CrawlThreshold = 0.60f;

	// 완전 행동불능(쓰러짐) 임계값 [0,1]
	UPROPERTY(EditAnywhere, Category="Incapacitation")
	float IncapacitationThreshold = 0.90f;

	// 기어갈 때 이동 속도 (cm/s)
	UPROPERTY(EditAnywhere, Category="Incapacitation")
	float CrawlSpeed = 80.f;
};
