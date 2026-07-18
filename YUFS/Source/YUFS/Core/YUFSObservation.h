// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Core/YUFSTypes.h"
#include "YUFSObservation.generated.h"

USTRUCT(BlueprintType)
struct YUFS_API FYUFSNPCObservation
{
	GENERATED_BODY()

	UPROPERTY()
	float SmokeDensityAtSelf = 0.0f;
	UPROPERTY()
	float TemperatureAtSelf = 0.0f;
	UPROPERTY()
	float SmokeInFrontNormalized = 0.0f;
	UPROPERTY()
	float SmokeAboveNormalized = 0.0f;
	UPROPERTY()
	float RiskLevel = 0.0f;
	UPROPERTY()
	float SimTimeNormalized = 0.0f;

	UPROPERTY()
	float DistToNearestExit = 0.0f;
	UPROPERTY()
	float DistToFamiliarExit = 0.0f;
	UPROPERTY()
	FVector DirToNearestExit = FVector::ZeroVector;
	UPROPERTY()
	bool bNearestExitSmokeFree = false;

	UPROPERTY()
	float NearbyEvacuatingRatio = 0.0f;
	UPROPERTY()
	int32 NearbyNPCCount = 0;
	UPROPERTY()
	int32 GroupSize = 0;
	UPROPERTY()
	bool bNearbyNPCNeedsHelp = false;

	UPROPERTY()
	bool bAlarmSounding = false;
	UPROPERTY()
	bool bReceivedPreRecordedMsg = false;
	UPROPERTY()
	bool bReceivedLiveAnnouncement = false;
	UPROPERTY()
	bool bReceivedStaffGuidance = false;
	UPROPERTY()
	FVector StaffGuidedExitLocation = FVector::ZeroVector;

	UPROPERTY()
	EYUFSBehaviorState CurrentState = EYUFSBehaviorState::Normal;
	UPROPERTY()
	float RiskPerception = 0.0f;
	UPROPERTY()
	float StressLevel = 0.0f;
	UPROPERTY()
	int32 MillingActionCount = 0;
	UPROPERTY()
	float SmokeExposureAccumulated = 0.0f; // 누적 연기 흡입량 [0,1] — 행동불능 결정 변수

	TArray<float> ToFloatArray() const;
};
