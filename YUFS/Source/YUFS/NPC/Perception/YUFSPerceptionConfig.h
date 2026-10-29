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
	UPROPERTY(EditAnywhere) float SmokeAwarenessThreshold  = 0.15f;
	UPROPERTY(EditAnywhere) float IncapacitationThreshold  = 0.70f;
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
	// Normalized heat data, not rendered flame recognition. Near probes cannot cross collision walls/floors.
	UPROPERTY(EditAnywhere) float NearHeatRange = 180.f;
	UPROPERTY(EditAnywhere) float MaxProbeStepCm = 40.f;
	// Presentation/cognition calibration against normalized BIN density; not physical extinction units.
	UPROPERTY(EditAnywhere, meta=(ClampMin="0", ClampMax="10")) float SmokeSightAttenuationPerMeter = 1.2f;
	UPROPERTY(EditAnywhere, meta=(ClampMin="0.01", ClampMax="0.95")) float MinimumSightTransmission = 0.15f;
	UPROPERTY(EditAnywhere) bool bDrawVisionDebug          = false;
};
