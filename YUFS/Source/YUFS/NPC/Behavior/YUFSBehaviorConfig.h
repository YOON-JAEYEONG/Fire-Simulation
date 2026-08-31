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
	// ppt.html의 초기값. 프로젝트 데이터에 맞게 Data Asset에서 조정한다.
	UPROPERTY(EditAnywhere, Category="Decision|Commit", meta=(ClampMin="0.0", ClampMax="1.0"))
	float AlarmOnlyImmediateEvacuationProbability = 0.25f;

	UPROPERTY(EditAnywhere, Category="Decision|Commit", meta=(ClampMin="0.0", ClampMax="1.0"))
	float SmokeImmediateEvacuationProbability = 0.65f;

	UPROPERTY(EditAnywhere, Category="Decision|Commit", meta=(ClampMin="0.0", ClampMax="1.0"))
	float FlameOrHeatImmediateEvacuationProbability = 0.90f;

	UPROPERTY(EditAnywhere, Category="Decision|Commit", meta=(ClampMin="0.01"))
	float SafetyTrainingLikelihoodRatio = 1.40f;

	UPROPERTY(EditAnywhere, Category="Decision|Commit", meta=(ClampMin="0.01"))
	float OfficialInformationLikelihoodRatio = 2.20f;

	UPROPERTY(EditAnywhere, Category="Decision|Commit", meta=(ClampMin="0.01"))
	float MovingCrowdLikelihoodRatio = 1.50f;

	UPROPERTY(EditAnywhere, Category="Decision|Commit", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MovingCrowdRatioThreshold = 0.30f;

	UPROPERTY(EditAnywhere, Category="Decision|Commit", meta=(ClampMin="0.0", ClampMax="1.0"))
	float FlameOrHighHeatTemperatureThreshold = 0.65f;

	// 사전행동 개수 구간: 1~5 / 6~9 / 10~15의 사람 비율.
	UPROPERTY(EditAnywhere, Category="Decision|PreEvacuation", meta=(ClampMin="0.0"))
	float ShortActionCountWeight = 88.5f;

	UPROPERTY(EditAnywhere, Category="Decision|PreEvacuation", meta=(ClampMin="0.0"))
	float MediumActionCountWeight = 8.1f;

	UPROPERTY(EditAnywhere, Category="Decision|PreEvacuation", meta=(ClampMin="0.0"))
	float LongActionCountWeight = 3.4f;

	// 개인의 발생 확률이 아니라 사전행동 풀 내 선택 비중이다.
	UPROPERTY(EditAnywhere, Category="Decision|PreEvacuation", meta=(ClampMin="0.0"))
	float SeekInformationWeight = 45.f;

	UPROPERTY(EditAnywhere, Category="Decision|PreEvacuation", meta=(ClampMin="0.0"))
	float WaitAndObserveWeight = 20.f;

	UPROPERTY(EditAnywhere, Category="Decision|PreEvacuation", meta=(ClampMin="0.0"))
	float GatherBelongingsWeight = 20.f;

	UPROPERTY(EditAnywhere, Category="Decision|PreEvacuation", meta=(ClampMin="0.0"))
	float AlertAndHelpWeight = 10.f;

	UPROPERTY(EditAnywhere, Category="Decision|PreEvacuation", meta=(ClampMin="0.0"))
	float InitialFirefightingWeight = 5.f;

	UPROPERTY(EditAnywhere, Category="Decision|Duration")
	FVector2D SeekInformationDurationRange = FVector2D(4.f, 8.f);

	UPROPERTY(EditAnywhere, Category="Decision|Duration")
	FVector2D WaitAndObserveDurationRange = FVector2D(3.f, 6.f);

	UPROPERTY(EditAnywhere, Category="Decision|Duration")
	FVector2D GatherBelongingsDurationRange = FVector2D(3.f, 8.f);

	UPROPERTY(EditAnywhere, Category="Decision|Duration")
	FVector2D AlertAndHelpDurationRange = FVector2D(2.f, 5.f);

	UPROPERTY(EditAnywhere, Category="Decision|Duration")
	FVector2D InitialFirefightingDurationRange = FVector2D(4.f, 10.f);

	// 70/20/10은 확률적 경로 효용 모델의 캘리브레이션 사전값이다.
	UPROPERTY(EditAnywhere, Category="Decision|Route", meta=(ClampMin="0.0"))
	float FamiliarRoutePriorWeight = 70.f;

	UPROPERTY(EditAnywhere, Category="Decision|Route", meta=(ClampMin="0.0"))
	float CrowdOrLeaderRoutePriorWeight = 20.f;

	UPROPERTY(EditAnywhere, Category="Decision|Route", meta=(ClampMin="0.0"))
	float NearestSafeRoutePriorWeight = 10.f;

	UPROPERTY(EditAnywhere, Category="Decision|Route", meta=(ClampMin="0.0"))
	float RouteDistanceUtilityScale = 0.50f;

	UPROPERTY(EditAnywhere, Category="Decision|Route", meta=(ClampMin="0.0"))
	float CrowdEvidenceUtilityScale = 1.00f;

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
