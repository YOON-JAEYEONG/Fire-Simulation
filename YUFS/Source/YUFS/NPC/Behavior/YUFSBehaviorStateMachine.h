// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "YUFSBehaviorConfig.h"
#include "Components/ActorComponent.h"
#include "Core/YUFSTypes.h"
#include "YUFSBehaviorStateMachine.generated.h"


struct FYUFSNPCObservation;
UENUM(BlueprintType)
enum class EYUFSEvacuationCue : uint8 { None, Alarm, Smoke, Heat, PeerWarning, Crowd, Guidance };

UCLASS(ClassGroup=(YUFS), meta=(BlueprintSpawnableComponent))
class YUFS_API UYUFSBehaviorStateMachine : public UActorComponent
{
	GENERATED_BODY()

public:
	UYUFSBehaviorStateMachine();

public:
	virtual void BeginPlay() override;
	void InitializePersonality(int32 Seed);
	UFUNCTION(BlueprintPure) EYUFSEvacuationCue GetDecisionCue() const { return DecisionCue; }
	UFUNCTION(BlueprintPure) float GetAlarmTrust() const { return AlarmTrust; }
	UFUNCTION(BlueprintPure) bool HasRecentDirectEvidence() const { return DirectEvidenceAge < 5.f; }
	float GetSpeedMultiplier() const { return SpeedMultiplier; }
	float GetRoutePreference() const { return RoutePreference; }
	bool HasCommittedToEvacuation() const { return bCommitted; }
	void TickStateMachine(float DeltaTime, const FYUFSNPCObservation& Obs);

	EYUFSBehaviorState GetCurrentState() const { return CurrentState; }
	float GetRiskPerception() const { return RiskPerception; }
	float GetSmokeExposure() const { return SmokeExposureAccumulated; }
	bool IsCrawling() const { return CurrentState == EYUFSBehaviorState::Crawling; }
	bool IsIncapacitated() const { return CurrentState == EYUFSBehaviorState::Incapacitated; }

	// Communication System 이벤트 수신
	void OnAlarmReceived();
	void OnPreRecordedMessageReceived();
	void OnStaffGuidanceReceived();
	void OnLiveAnnouncementReceived();

	UPROPERTY(EditAnywhere)
	UYUFSBehaviorConfig* Config;

private:
	float AlarmTrust = 0.5f;
	float Sensitivity = 1.f;
	float ResponseDelay = 2.f;
	float AlarmDecisionTime = 25.f;
	float PreparationScale = 1.f;
	float SpeedMultiplier = 1.f;
	float RoutePreference = 0.5f;
	float AlarmElapsed = 0.f;
	float EvidenceTime = 0.f;
	float DirectEvidenceAge = 10000.f;
	bool bCommitted = false;
	EYUFSEvacuationCue DecisionCue = EYUFSEvacuationCue::None;
	EYUFSBehaviorState CurrentState = EYUFSBehaviorState::Normal;
	float StateTimer = 0.f;
	float RiskPerception = 0.f;
	float SmokeExposureAccumulated = 0.f; // 누적 연기 흡입량 [0,1]

	// 상태별 전이 조건
	void TryTransition(const FYUFSNPCObservation& Obs);
	void AccumulateRiskPerception(const FYUFSNPCObservation& Obs, float DeltaTime);
	void AccumulateSmokeExposure(const FYUFSNPCObservation& Obs, float DeltaTime);

	// 긴급 오버라이드: 연기 임계값 2배 초과 시 즉시 Evacuating
	bool CheckEmergencyOverride(const FYUFSNPCObservation& Obs) const;
};
