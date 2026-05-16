// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Components/ActorComponent.h"
#include "NavigationSystem.h"
#include "YUFSSmokeAwareNavigator.generated.h"


class AYUFSLevelDataManager;
class AYUFSBinaryManager;

UCLASS(ClassGroup=(YUFS), meta=(BlueprintSpawnableComponent))
class YUFS_API UYUFSSmokeAwareNavigator : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UYUFSSmokeAwareNavigator();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

public:
	// 목적지까지 연기 회피 경로 요청 (비동기)
	void RequestPathAsync(FVector Destination, int32 Frame);

	void OnPathFound(uint32 PathId, ENavigationQueryResult::Type Result, FNavPathSharedPtr NavPath);

	bool bIsPathfinding = false;

	// 현재 경로가 연기로 차단됐는지 주기 확인 → 차단 시 재탐색
	void CheckAndReroute(int32 Frame);

	// 경로 강제 초기화 (Stuck 발생 시 호출)
	void ClearPath();

	// 다음 이동 목표 지점 (NPC가 매 Tick 읽어감)
	FVector GetNextWaypoint() const;

	// NPC가 현재 목표 지점에 도달했는지 확인하고 다음 지점으로 인덱스 업데이트
	void UpdateWaypoint(FVector ActorLocation, float AcceptanceRadius = 50.f);

	FVector GetCurrentDestination() const { return CurrentDestination; }
	const TArray<FVector>& GetCurrentPathPoints() const { return CurrentPath; }
	int32 GetCurrentWaypointIndex() const { return CurrentWaypointIndex; }

	UPROPERTY(EditAnywhere)
	float RerouteCheckInterval = 2.0f;
	UPROPERTY(EditAnywhere)
	float SmokeBlockThreshold = 0.4f;

private:
	float EvaluatePathCost(const TArray<FVector>& Path, int32 Frame) const;

	UPROPERTY()
	AYUFSLevelDataManager* LevelDataMgr;

	TArray<FVector> CurrentPath;
	int32 CurrentWaypointIndex = 0;
	FVector CurrentDestination = FVector::ZeroVector;
	float RerouteTimer = 0.f;
};
