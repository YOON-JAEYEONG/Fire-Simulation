// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Components/ActorComponent.h"
#include "NavigationSystem.h"
#include "NPC/Navigation/YUFSSmokeNavigationQueryFilter.h"
#include "YUFSSmokeAwareNavigator.generated.h"

class AYUFSLevelDataManager;
class AYUFSBinaryManager;

UCLASS(ClassGroup=(YUFS), meta=(BlueprintSpawnableComponent))
class YUFS_API UYUFSSmokeAwareNavigator : public UActorComponent
{
	GENERATED_BODY()

public:
	UYUFSSmokeAwareNavigator();

protected:
	virtual void BeginPlay() override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

public:
	void RequestPathAsync(FVector Destination, int32 Frame);

	bool bIsPathfinding = false;

	void CheckAndReroute(int32 Frame);

	void ClearPath();

	FVector GetNextWaypoint() const;
	FVector GetSteeringTarget(FVector ActorLocation, float LookAheadDistance = 250.f) const;

	void UpdateWaypoint(FVector ActorLocation, float AcceptanceRadius = 50.f);

	FVector GetCurrentDestination() const { return CurrentDestination; }
	const TArray<FVector>& GetCurrentPathPoints() const { return CurrentPath; }
	int32 GetCurrentWaypointIndex() const { return CurrentWaypointIndex; }

	UPROPERTY(EditAnywhere)
	float RerouteCheckInterval = 2.0f;

	UPROPERTY(EditAnywhere)
	float SmokeBlockThreshold = 0.4f;

private:
	UPROPERTY()
	AYUFSLevelDataManager* LevelDataMgr = nullptr;

	UPROPERTY()
	AYUFSBinaryManager* BinaryManager = nullptr;

	TArray<FVector> CurrentPath;
	int32 CurrentWaypointIndex = 0;
	FVector CurrentDestination = FVector::ZeroVector;
	float RerouteTimer = 0.f;
};
