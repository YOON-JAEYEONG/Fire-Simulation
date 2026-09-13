#pragma once

#include "Components/ActorComponent.h"
#include "NavigationSystem.h"
#include "Fire/YUFSHazardField.h"
#include "YUFSSmokeAwareNavigator.generated.h"

class AYUFSLevelDataManager;

UENUM(BlueprintType)
enum class EYUFSNavigationStatus : uint8
{
	Idle, Pathfinding, Moving, Arrived, Failed, WaitingForHazardData
};

UENUM(BlueprintType)
enum class EYUFSRepathReason : uint8
{
	DestinationRequested, Smoke, Stuck, Retry
};

UENUM(BlueprintType)
enum class EYUFSNavigationFailure : uint8
{
	None, InvalidOwner, InvalidDestination, NoNavigationData,
	StartOffNavMesh, DestinationOffNavMesh, NoPath, PartialPath,
	HazardDataUnavailable, UnsafePath, UnsupportedNavData
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FYUFSNavigationStateChanged,
	EYUFSNavigationStatus, Status, FVector, Destination, EYUFSNavigationFailure, Failure);

UCLASS(ClassGroup=(YUFS), meta=(BlueprintSpawnableComponent))
class YUFS_API UYUFSSmokeAwareNavigator : public UActorComponent
{
	GENERATED_BODY()

public:
	UYUFSSmokeAwareNavigator();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	// A new destination replaces any pending request. (0,0,0) is a valid destination.
	UFUNCTION(BlueprintCallable, Category="YUFS|Navigation")
	void RequestPathAsync(FVector Destination, int32 Frame);

	UFUNCTION(BlueprintCallable, Category="YUFS|Navigation")
	void ReplanPath(int32 Frame, EYUFSRepathReason Reason);

	UFUNCTION(BlueprintCallable, Category="YUFS|Navigation")
	void ClearPath();

	void CheckAndReroute(int32 Frame);
	FVector GetNextWaypoint() const;
	FVector GetSteeringTarget(FVector ActorLocation, float LookAheadDistance = 250.f) const;
	void UpdateWaypoint(FVector ActorLocation, float AcceptanceRadius = 50.f);

	UFUNCTION(BlueprintPure, Category="YUFS|Navigation")
	EYUFSNavigationStatus GetNavigationStatus() const { return NavigationStatus; }
	UFUNCTION(BlueprintPure, Category="YUFS|Navigation")
	EYUFSNavigationFailure GetLastFailure() const { return LastFailure; }
	UFUNCTION(BlueprintPure, Category="YUFS|Navigation")
	EYUFSRepathReason GetLastRepathReason() const { return LastRepathReason; }
	UFUNCTION(BlueprintPure, Category="YUFS|Navigation")
	FVector GetCurrentDestination() const { return CurrentDestination; }
	UFUNCTION(BlueprintPure, Category="YUFS|Navigation")
	FVector GetRequestedDestination() const { return RequestedDestination; }
	UFUNCTION(BlueprintPure, Category="YUFS|Navigation")
	bool IsFollowingPath() const { return NavigationStatus == EYUFSNavigationStatus::Moving; }
	bool ShouldRetryPath() const;
	UFUNCTION(BlueprintPure, Category="YUFS|Navigation")
	EYUFSHazardDataStatus GetHazardDataStatus() const { return HazardDataStatus; }
	UFUNCTION(BlueprintPure, Category="YUFS|Navigation")
	float GetPathSmoke() const { return LastPathScore.MaxSmoke; }
	UFUNCTION(BlueprintPure, Category="YUFS|Navigation")
	float GetPathHeat() const { return LastPathScore.MaxHeat; }
	FYUFSHazardSnapshot GetPerceivedHazardSnapshot(int32 Frame) const;
	bool IsLocalRecoverySafe(const FVector& FromFeet, const FVector& ToFeet, int32 Frame) const;
	const TArray<FVector>& GetCurrentPathPoints() const { return CurrentPath; }
	int32 GetCurrentWaypointIndex() const { return CurrentWaypointIndex; }

	UPROPERTY(BlueprintAssignable, Category="YUFS|Navigation")
	FYUFSNavigationStateChanged OnNavigationStateChanged;

	// Kept for existing C++ callers; status changes are owned by this component.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="YUFS|Navigation")
	bool bIsPathfinding = false;
	UPROPERTY(EditAnywhere, Category="YUFS|Navigation", meta=(ClampMin="0.1"))
	float RerouteCheckInterval = 0.5f;
	UPROPERTY(EditAnywhere, Category="YUFS|Navigation", meta=(ClampMin="0.0"))
	float SmokeBlockThreshold = 0.4f;
	UPROPERTY(EditAnywhere, Category="YUFS|Navigation|Hazard", meta=(ClampMin="0.0"))
	float SmokeTravelCost = 30.f;
	UPROPERTY(EditAnywhere, Category="YUFS|Navigation|Hazard", meta=(ClampMin="0.0"))
	float HeatTravelCost = 60.f;
	UPROPERTY(EditAnywhere, Category="YUFS|Navigation|Hazard", meta=(ClampMin="0.01", ClampMax="1.0"))
	float UnsafeSmokeThreshold = 0.7f;
	UPROPERTY(EditAnywhere, Category="YUFS|Navigation|Hazard", meta=(ClampMin="0.01", ClampMax="1.0"))
	float UnsafeHeatThreshold = 0.7f;
	UPROPERTY(EditAnywhere, Category="YUFS|Navigation|Hazard", meta=(ClampMin="0.0"))
	float HazardSampleHeightCm = 120.f;
	UPROPERTY(EditAnywhere, Category="YUFS|Navigation", meta=(ClampMin="0.1"))
	float FailedPathRetryInterval = 2.f;
	UPROPERTY(EditAnywhere, Category="YUFS|Navigation", meta=(ClampMin="0"))
	int32 MaxFailedPathRetries = 2;
	UPROPERTY(EditAnywhere, Category="YUFS|Navigation", meta=(ClampMin="1.0"))
	float WaypointHeightTolerance = 150.f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void StartPathRequest(FVector Destination, int32 Frame, EYUFSRepathReason Reason);
	void CancelPendingRequest();
	void OnPathFound(uint32 QueryId, ENavigationQueryResult::Type Result,
		FNavPathSharedPtr Path, uint32 Generation);
	void SetNavigationStatus(EYUFSNavigationStatus Status,
		EYUFSNavigationFailure Failure = EYUFSNavigationFailure::None);
	void StopOwnerMovement();
	TArray<FVector> BuildRemainingPath() const;
	FVector GetOwnerFeetLocation() const;
	FYUFSHazardSettings GetHazardSettings() const;

	UPROPERTY()
	AYUFSLevelDataManager* LevelDataMgr = nullptr;
	TWeakObjectPtr<UNavigationSystemV1> PendingNavigationSystem;
	TArray<FVector> CurrentPath;
	FVector RequestedDestination = FVector::ZeroVector;
	FVector CurrentDestination = FVector::ZeroVector;
	int32 CurrentWaypointIndex = 0;
	uint32 ActiveQueryId = INVALID_NAVQUERYID;
	uint32 RequestGeneration = 0;
	int32 RequestFrame = 0;
	int32 FailedPathRetries = 0;
	float RerouteTimer = 0.f;
	float TimeSinceRequest = 0.f;
	FYUFSHazardSnapshot QueryHazardSnapshot;
	FYUFSHazardPathScore LastPathScore;
	EYUFSHazardDataStatus HazardDataStatus = EYUFSHazardDataStatus::MissingData;
	EYUFSNavigationStatus NavigationStatus = EYUFSNavigationStatus::Idle;
	EYUFSNavigationFailure LastFailure = EYUFSNavigationFailure::None;
	EYUFSRepathReason LastRepathReason = EYUFSRepathReason::DestinationRequested;

	// Regression tests deliver out-of-order engine callbacks without timing races.
	friend struct FYUFSNavigationTestAccess;
	friend struct FYUFSCrowdIntegrationAccess;
};
