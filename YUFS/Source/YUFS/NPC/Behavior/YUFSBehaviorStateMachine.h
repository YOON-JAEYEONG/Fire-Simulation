// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "YUFSBehaviorConfig.h"
#include "Components/ActorComponent.h"
#include "Core/YUFSTypes.h"
#include "YUFSBehaviorStateMachine.generated.h"


struct FYUFSNPCObservation;

UCLASS(ClassGroup=(YUFS), meta=(BlueprintSpawnableComponent))
class YUFS_API UYUFSBehaviorStateMachine : public UActorComponent
{
	GENERATED_BODY()

public:
	UYUFSBehaviorStateMachine();

public:
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
