// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "YUFSPerceptionConfig.generated.h"

/**
 * 
 */
UCLASS()
class YUFS_API UYUFSPerceptionConfig : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere) float SmokeAwarenessThreshold  = 0.15f; // Perceiving threshold
	UPROPERTY(EditAnywhere) float IncapacitationThreshold  = 0.70f; // Incapacitation threshold
	UPROPERTY(EditAnywhere) float PathSampleLookAheadDist  = 300.f; // Forward path sample distance (cm)
	UPROPERTY(EditAnywhere) int32 PathSampleCount          = 4;     // Forward path sample count
	UPROPERTY(EditAnywhere) float VisionRange              = 1000.f;
	UPROPERTY(EditAnywhere) float FieldOfViewDegrees       = 90.f;
	UPROPERTY(EditAnywhere) int32 VisionRayCount           = 5;
	UPROPERTY(EditAnywhere) int32 VisionSamplesPerRay      = 5;
	UPROPERTY(EditAnywhere) float OcclusionSampleMarginCm  = 5.f;
	UPROPERTY(EditAnywhere) float UpperVisionPitchLowDegrees = 20.f;
	UPROPERTY(EditAnywhere) float UpperVisionPitchHighDegrees = 40.f;
	UPROPERTY(EditAnywhere) float UpperVisionYawHalfSpreadDegrees = 25.f;
	UPROPERTY(EditAnywhere) int32 UpperVisionYawRayCount = 3;
	UPROPERTY(EditAnywhere) float OverheadProbePitchDegrees = 75.f;
	UPROPERTY(EditAnywhere) float OverheadProbeRange = 350.f;
	UPROPERTY(EditAnywhere) bool bDrawVisionDebug          = false;
};
