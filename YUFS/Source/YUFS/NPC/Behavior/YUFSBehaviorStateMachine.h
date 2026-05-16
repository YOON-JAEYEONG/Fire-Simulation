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
	// Sets default values for this component's properties
	UYUFSBehaviorStateMachine();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

public:
	void TickStateMachine(float DeltaTime, const FYUFSNPCObservation& Obs);

	EYUFSBehaviorState GetCurrentState() const { return CurrentState; }
	float GetRiskPerception() const { return RiskPerception; }

	// Communication System 이벤트 수신
	void OnAlarmReceived();
	void OnStaffGuidanceReceived();
	void OnLiveAnnouncementReceived();

	UPROPERTY(EditAnywhere)
	UYUFSBehaviorConfig* Config;

private:
	EYUFSBehaviorState CurrentState = EYUFSBehaviorState::Normal;
	float StateTimer = 0.f;
	float RiskPerception = 0.f; // 누적 위험 인식값 — 핵심 대피 결정 변수

	// 상태별 전이 조건 (각 cpp에서 구현)
	void TryTransition(const FYUFSNPCObservation& Obs);
	void AccumulateRiskPerception(const FYUFSNPCObservation& Obs, float DeltaTime);

	// 긴급 오버라이드: 연기 임계값 2배 초과 시 즉시 Evacuating
	bool CheckEmergencyOverride(const FYUFSNPCObservation& Obs) const;
};
