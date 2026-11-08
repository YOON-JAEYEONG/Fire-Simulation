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
	// Design parameters for experiments, not population statistics.
	UPROPERTY(EditAnywhere, Category="Individual") float EarlyAlarmResponseFraction = 0.30f;
	UPROPERTY(EditAnywhere, Category="Individual") float VerifyAlarmResponseFraction = 0.45f;
	UPROPERTY(EditAnywhere, Category="Individual") float HeatAwarenessThreshold = 0.20f;
	UPROPERTY(EditAnywhere, Category="Individual") float EmergencyHeatThreshold = 0.65f;
	UPROPERTY(EditAnywhere, Category="Individual") float EvidenceMemorySeconds = 20.f;
	UPROPERTY(EditAnywhere, Category="Individual") int32 PersonalitySeed = 173;
	UPROPERTY(EditAnywhere)
	float MaxMillingDuration = 30.f;

	// Perceiving 상태 유지 시간 — 이후 Milling으로 전환 (PADM: 단서 인지 후 상황 파악 시작)
	UPROPERTY(EditAnywhere)
	float PerceivinDuration = 2.f;

	UPROPERTY(EditAnywhere)
	float PreparationDuration = 5.f;

	// Helping 상태에서 최대 도움 지속 시간 — 초과 시 Evacuating으로 복귀
	UPROPERTY(EditAnywhere)
	float MaxHelpingDuration = 15.f;
	
	UPROPERTY(EditAnywhere)
	float RiskPerceptionThreshold = 0.40f;

	UPROPERTY(EditAnywhere)
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
	// Normalized exposure per second at smoke=1; not a physiological dose model.
	UPROPERTY(EditAnywhere, Category="Incapacitation")
	float SmokeExposureAccumRate = 0.008f;
	// Short demonstration runs: 3x gives crawl ~25s / incapacitation ~37.5s at constant smoke=1.
	// Set to 1 to restore the earlier 75s / 112.5s timing. Shared assets are never mutated at runtime.
	UPROPERTY(EditAnywhere, Category="Incapacitation|Simulation Timing", meta=(ClampMin="0.1", ClampMax="10.0"))
	float SmokeExposureTimeScale = 3.f;

	// 행동불능 전 '기어가기' 단계 진입 임계값 [0,1]
	UPROPERTY(EditAnywhere, Category="Incapacitation")
	float CrawlThreshold = 0.60f;
	// Hysteresis prevents repeatedly standing into the smoke layer immediately after lowering the head.
	UPROPERTY(EditAnywhere, Category="Incapacitation", meta=(ClampMin="0.0", ClampMax="1.0"))
	float CrawlRecoveryThreshold = 0.45f;

	// 완전 행동불능(쓰러짐) 임계값 [0,1]
	UPROPERTY(EditAnywhere, Category="Incapacitation")
	float IncapacitationThreshold = 0.90f;

	// 기어갈 때 이동 속도 (cm/s)
	UPROPERTY(EditAnywhere, Category="Incapacitation")
	float CrawlSpeed = 80.f;

	// Vertical collision approximation; runtime clamps half-height to the capsule radius.
	UPROPERTY(EditAnywhere, Category="Incapacitation|Posture", meta=(ClampMin="1.0"))
	float CrawlCapsuleHalfHeightCm = 44.f;
	// Height above the capsule feet, not above its moving centre. Not a physiological calibration.
	UPROPERTY(EditAnywhere, Category="Incapacitation|Posture", meta=(ClampMin="1.0"))
	float CrawlBreathingHeightCm = 55.f;
};
