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
	float SmokeDensityAtSelf;
	UPROPERTY()
	float TemperatureAtSelf;
	UPROPERTY()
	float SmokeInFrontNormalized;
	UPROPERTY()
	float SmokeAboveNormalized;
	UPROPERTY()
	float RiskLevel;
	UPROPERTY()
	float SimTimeNormalized;

	UPROPERTY()
	float DistToNearestExit;
	UPROPERTY()
	float DistToFamiliarExit;
	UPROPERTY()
	FVector DirToNearestExit;
	UPROPERTY()
	bool bNearestExitSmokeFree;

	UPROPERTY()
	float NearbyEvacuatingRatio;
	UPROPERTY()
	int32 NearbyNPCCount;
	UPROPERTY()
	int32 GroupSize;
	UPROPERTY()
	bool bNearbyNPCNeedsHelp;

	UPROPERTY()
	bool bAlarmSounding;
	UPROPERTY()
	bool bReceivedPreRecordedMsg;
	UPROPERTY()
	bool bReceivedLiveAnnouncement;
	UPROPERTY()
	bool bReceivedStaffGuidance;
	UPROPERTY()
	FVector StaffGuidedExitLocation;

	UPROPERTY()
	EYUFSBehaviorState CurrentState;
	UPROPERTY()
	float RiskPerception;
	UPROPERTY()
	float StressLevel;
	UPROPERTY()
	int32 MillingActionCount;
	UPROPERTY()
	float SmokeExposureAccumulated; // 누적 연기 흡입량 [0,1] — 행동불능 결정 변수

	TArray<float> ToFloatArray() const;
};
