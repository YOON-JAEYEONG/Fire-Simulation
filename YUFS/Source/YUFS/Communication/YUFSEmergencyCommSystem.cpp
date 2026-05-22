// Fill out your copyright notice in the Description page of Project Settings.


#include "Communication/YUFSEmergencyCommSystem.h"

#include "YUFSCommTypes.h"

// Sets default values
AYUFSEmergencyCommSystem::AYUFSEmergencyCommSystem()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
	
	RootComponent = CreateDefaultSubobject<USceneComponent>("RootComponent");;
}

// Called when the game starts or when spawned
void AYUFSEmergencyCommSystem::BeginPlay()
{
	Super::BeginPlay();
}

void AYUFSEmergencyCommSystem::ActivateAlarm()
{
	// 논문: 알람은 대피를 지연시키고 촬영 행동을 유발할 수 있음 [cite: 1550, 1761]
	OnEmergencyComm.Broadcast(EYUFSCommType::AlarmOnly, GetActorLocation(), AlarmRadius, FVector::ZeroVector);
}

void AYUFSEmergencyCommSystem::BroadcastPreRecordedMessage(FText Message)
{
	OnEmergencyComm.Broadcast(EYUFSCommType::PreRecordedMessage, GetActorLocation(), AnnouncementRadius, FVector::ZeroVector);
}

void AYUFSEmergencyCommSystem::BroadcastLiveAnnouncement(FText Message)
{
	// 논문: 실시간 안내는 알람보다 빠른 반응을 유도함 [cite: 1579, 1762]
	OnEmergencyComm.Broadcast(EYUFSCommType::LiveAnnouncement, GetActorLocation(), AnnouncementRadius, FVector::ZeroVector);
}

void AYUFSEmergencyCommSystem::DispatchStaffGuidance(FVector TargetExit)
{
	// 논문: 스태프의 직접 안내는 가장 효과적인 대피 유도 수단임 (OR=0.33) [cite: 1549, 1804]
	OnEmergencyComm.Broadcast(EYUFSCommType::StaffGuidance, GetActorLocation(), StaffGuidanceRadius, TargetExit);
}

void AYUFSEmergencyCommSystem::NotifyNPCsInRadius(float Radius, EYUFSCommType CommType, FVector GuidanceTarget)
{
	OnEmergencyComm.Broadcast(CommType, GetActorLocation(), Radius, GuidanceTarget);
}