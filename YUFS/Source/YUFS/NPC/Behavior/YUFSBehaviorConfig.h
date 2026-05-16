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
};
