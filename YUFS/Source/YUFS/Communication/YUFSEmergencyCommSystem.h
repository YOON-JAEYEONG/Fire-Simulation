// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "YUFSEmergencyCommSystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
	FOnEmergencyComm,
	EYUFSCommType, CommType,
	FVector, SourceLocation, // 방송 발원 위치
	float, EffectiveRadius, // 유효 반경
	FVector, GuidanceTarget // StaffGuidance 목적지 (나머지는 ZeroVector)
);

UCLASS()
class YUFS_API AYUFSEmergencyCommSystem : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AYUFSEmergencyCommSystem();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// 4가지 통신 수단 (논문 van der Wal et al. 기반)
	UFUNCTION(BlueprintCallable)
	void ActivateAlarm();
	void BroadcastPreRecordedMessage(FText Message);
	void BroadcastLiveAnnouncement(FText Message);
	void DispatchStaffGuidance(FVector TargetExit);
	void NotifyNPCsInRadius(float Radius, EYUFSCommType CommType, FVector GuidanceTarget = FVector::ZeroVector);

	// NPC가 이벤트 구독 — 직접 참조 없이 느슨하게 결합
	UPROPERTY(BlueprintAssignable)
	FOnEmergencyComm OnEmergencyComm;

	UPROPERTY(EditAnywhere)
	float AlarmRadius = 3000.f;
	UPROPERTY(EditAnywhere)
	float AnnouncementRadius = 5000.f;
	UPROPERTY(EditAnywhere)
	float StaffGuidanceRadius = 800.f;
};
