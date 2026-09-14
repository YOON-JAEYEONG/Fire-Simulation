#pragma once
#include "Components/ActorComponent.h"
#include "NavigationSystem.h"
#include "NPC/Navigation/YUFSSmokeNavigationQueryFilter.h"
#include "YUFSSmokeAwareNavigator.generated.h"

UENUM(BlueprintType)
enum class EYUFSNavigationStatus : uint8 { Idle, Pathfinding, Moving, Arrived, Failed };
UENUM(BlueprintType)
enum class EYUFSNavigationFailure : uint8
{
 None, InvalidOwner, InvalidDestination, NoNavigationData, StartOffNavMesh,
 DestinationOffNavMesh, NoPath, PartialPath, UnsafeKnownPath, UnsupportedNavData, Blocked
};
UCLASS(ClassGroup=(YUFS), meta=(BlueprintSpawnableComponent))
class YUFS_API UYUFSSmokeAwareNavigator : public UActorComponent
{
 GENERATED_BODY()
public:
 UYUFSSmokeAwareNavigator();
 virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
 // Latest request wins. Origin is valid. Interaction must not replace the requested goal.
 void RequestPathAsync(FVector Destination, int32 Frame);
 void CheckAndReroute(int32 Frame);
 void ClearPath();
 void ReportMovementBlocked();
 bool bIsPathfinding = false; // Compatibility; owned by this component.
 FVector GetNextWaypoint() const;
 FVector GetSteeringTarget(FVector ActorLocation, float LookAheadDistance = 250.f) const;
 void UpdateWaypoint(FVector ActorLocation, float AcceptanceRadius = 50.f);
 FVector GetCurrentDestination() const { return CurrentDestination; }
 FVector GetRequestedDestination() const { return RequestedDestination; }
 const TArray<FVector>& GetCurrentPathPoints() const { return CurrentPath; }
 int32 GetCurrentWaypointIndex() const { return CurrentWaypointIndex; }
 EYUFSNavigationStatus GetNavigationStatus() const { return Status; }
 EYUFSNavigationFailure GetLastFailure() const { return LastFailure; }
 uint32 GetRequestGeneration() const { return RequestGeneration; }
 bool IsFollowingPath() const { return Status == EYUFSNavigationStatus::Moving; }
 // Call only after real local/LOS observation. Never inject unseen global FDS data.
 void ReportObservedHazard(FVector WorldPosition, float Smoke01, float Heat01, float RadiusCm = 40.f);
 void ResetObservedHazards();
 FYUFSObservedHazardSnapshot GetObservedHazardSnapshot() const;
 bool IsKnownPathSafe(const TArray<FVector>& FloorPoints) const { return GetObservedHazardSnapshot().IsPathSafe(FloorPoints); }
 bool IsKnownLocationSafe(FVector Position) const { return GetObservedHazardSnapshot().IsLocationSafe(Position); }
 bool IsKnownPathDangerous(const TArray<FVector>& FloorPoints) const { return !IsKnownPathSafe(FloorPoints); }
 bool IsKnownLocationDangerous(FVector Position) const { return !IsKnownLocationSafe(Position); }
 UPROPERTY(EditAnywhere, Category="Navigation") float RerouteCheckInterval = 0.5f;
 UPROPERTY(EditAnywhere, Category="Navigation") float SmokeBlockThreshold = 0.4f;
 UPROPERTY(EditAnywhere, Category="Navigation") float UnsafeSmokeThreshold = 0.7f;
 UPROPERTY(EditAnywhere, Category="Navigation") float UnsafeHeatThreshold = 0.7f;
 UPROPERTY(EditAnywhere, Category="Navigation") float WaypointHeightTolerance = 150.f;
 UPROPERTY(EditAnywhere, Category="Navigation") float FailedPathRetryInterval = 2.f;
 UPROPERTY(EditAnywhere, Category="Navigation") int32 MaxFailedPathRetries = 2;
 UPROPERTY(EditAnywhere, Category="Navigation|Knowledge") float HazardMemorySeconds = 30.f;
protected:
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
 void CancelPendingRequest();
 void StopOwnerMovement();
 void SetStatus(EYUFSNavigationStatus NewStatus, EYUFSNavigationFailure Failure = EYUFSNavigationFailure::None);
 void OnPathFound(uint32 QueryId, ENavigationQueryResult::Type Result, FNavPathSharedPtr Path, uint32 Generation);
 void StartPathRequest(FVector Destination, int32 Frame);
 FVector GetOwnerFeetLocation() const;
 TArray<FVector> BuildRemainingPath() const;
 TWeakObjectPtr<UNavigationSystemV1> PendingNavigationSystem;
 uint32 ActiveQueryId = INVALID_NAVQUERYID, RequestGeneration = 0;
 TArray<FVector> CurrentPath;
 int32 CurrentWaypointIndex = 0;
 FVector RequestedDestination = FVector::ZeroVector, CurrentDestination = FVector::ZeroVector;
 FVector LastAttemptDestination = FVector::ZeroVector;
 bool bHasAttempted = false;
 int32 RequestFrame = 0, RetryCount = 0;
 uint32 KnowledgeRevision = 0, AttemptKnowledgeRevision = 0;
 float SinceAttempt = 0.f, RerouteTimer = 0.f;
 EYUFSNavigationStatus Status = EYUFSNavigationStatus::Idle;
 EYUFSNavigationFailure LastFailure = EYUFSNavigationFailure::None;
 bool bLastAttemptFailed = false;
 int32 RepeatedMovementBlocks = 0;
 FVector LastBlockedPosition = FVector::ZeroVector, LastBlockedGoal = FVector::ZeroVector;
 uint32 LastBlockedKnowledgeRevision = 0;
 TArray<FYUFSObservedHazard> ObservedHazards;
 FYUFSObservedHazardSnapshot QueryKnowledge;
 friend struct FYUFSInteractionNavigationTestAccess;
};
