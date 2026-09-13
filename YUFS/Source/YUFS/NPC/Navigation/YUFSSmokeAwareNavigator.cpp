#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"

#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Level/YUFSLevelDataManager.h"
#include "NavigationData.h"
#include "NPC/Perception/YUFSNPCPerceptionComponent.h"
#include "NPC/Navigation/YUFSSmokeNavigationQueryFilter.h"

UYUFSSmokeAwareNavigator::UYUFSSmokeAwareNavigator()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UYUFSSmokeAwareNavigator::BeginPlay()
{
	Super::BeginPlay();
	for (TActorIterator<AYUFSLevelDataManager> It(GetWorld()); It; ++It)
	{
		LevelDataMgr = *It;
		break;
	}
}

void UYUFSSmokeAwareNavigator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelPendingRequest();
	Super::EndPlay(EndPlayReason);
}

void UYUFSSmokeAwareNavigator::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	RerouteTimer += DeltaTime;
	TimeSinceRequest += DeltaTime;
	if (NavigationStatus == EYUFSNavigationStatus::WaitingForHazardData && TimeSinceRequest >= 0.5f)
	{
		StartPathRequest(RequestedDestination,
			IsValid(LevelDataMgr) ? LevelDataMgr->GetCurrentHazardFrame() : RequestFrame, LastRepathReason);
	}
}

void UYUFSSmokeAwareNavigator::CancelPendingRequest()
{
	// Abort only removes queued queries. A running query may still call back.
	++RequestGeneration;
	if (ActiveQueryId != INVALID_NAVQUERYID && PendingNavigationSystem.IsValid())
	{
		PendingNavigationSystem->AbortAsyncFindPathRequest(ActiveQueryId);
	}
	ActiveQueryId = INVALID_NAVQUERYID;
	PendingNavigationSystem.Reset();
	bIsPathfinding = false;
}

void UYUFSSmokeAwareNavigator::StopOwnerMovement()
{
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->ConsumeMovementInputVector();
		Character->GetCharacterMovement()->StopMovementImmediately();
	}
}

void UYUFSSmokeAwareNavigator::SetNavigationStatus(EYUFSNavigationStatus Status,
	EYUFSNavigationFailure Failure)
{
	NavigationStatus = Status;
	LastFailure = Failure;
	bIsPathfinding = Status == EYUFSNavigationStatus::Pathfinding;
	UE_LOG(LogTemp, Log,
		TEXT("[YUFS][Nav] agent=%s request=%u status=%s reason=%s failure=%s requested=%s destination=%s frame=%d data=%s smoke=%.3f heat=%.3f"),
		*GetNameSafe(GetOwner()), RequestGeneration,
		*StaticEnum<EYUFSNavigationStatus>()->GetNameStringByValue(static_cast<int64>(Status)),
		*StaticEnum<EYUFSRepathReason>()->GetNameStringByValue(static_cast<int64>(LastRepathReason)),
		*StaticEnum<EYUFSNavigationFailure>()->GetNameStringByValue(static_cast<int64>(Failure)),
		*RequestedDestination.ToCompactString(), *CurrentDestination.ToCompactString(), RequestFrame,
		*StaticEnum<EYUFSHazardDataStatus>()->GetNameStringByValue(static_cast<int64>(HazardDataStatus)),
		LastPathScore.MaxSmoke, LastPathScore.MaxHeat);
	OnNavigationStateChanged.Broadcast(Status, RequestedDestination, Failure);
}

void UYUFSSmokeAwareNavigator::RequestPathAsync(FVector Destination, int32 Frame)
{
	FailedPathRetries = 0;
	StartPathRequest(Destination, Frame, EYUFSRepathReason::DestinationRequested);
}

void UYUFSSmokeAwareNavigator::ReplanPath(int32 Frame, EYUFSRepathReason Reason)
{
	if (NavigationStatus == EYUFSNavigationStatus::Idle ||
		NavigationStatus == EYUFSNavigationStatus::Arrived || bIsPathfinding) return;
	if (Reason == EYUFSRepathReason::Retry)
	{
		if (!ShouldRetryPath()) return;
		++FailedPathRetries;
	}
	StartPathRequest(RequestedDestination, Frame, Reason);
}

bool UYUFSSmokeAwareNavigator::ShouldRetryPath() const
{
	const bool bChangedHazard = (LastFailure == EYUFSNavigationFailure::UnsafePath ||
		LastFailure == EYUFSNavigationFailure::HazardDataUnavailable) &&
		IsValid(LevelDataMgr) && LevelDataMgr->GetCurrentHazardFrame() != RequestFrame;
	return NavigationStatus == EYUFSNavigationStatus::Failed &&
		TimeSinceRequest >= FMath::Max(0.1f, FailedPathRetryInterval) &&
		(FailedPathRetries < FMath::Max(0, MaxFailedPathRetries) || bChangedHazard);
}

void UYUFSSmokeAwareNavigator::StartPathRequest(FVector Destination, int32 Frame, EYUFSRepathReason Reason)
{
	CancelPendingRequest();
	StopOwnerMovement();
	CurrentPath.Reset();
	CurrentWaypointIndex = 0;
	RequestedDestination = Destination;
	CurrentDestination = Destination;
	RequestFrame = Frame;
	LastRepathReason = Reason;
	TimeSinceRequest = 0.f;
	RerouteTimer = 0.f;
	QueryHazardSnapshot = FYUFSHazardSnapshot();

	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		SetNavigationStatus(EYUFSNavigationStatus::Failed, EYUFSNavigationFailure::InvalidOwner);
		return;
	}
	if (Destination.ContainsNaN())
	{
		SetNavigationStatus(EYUFSNavigationStatus::Failed, EYUFSNavigationFailure::InvalidDestination);
		return;
	}
	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
	const FNavAgentProperties& AgentProps = Character->GetNavAgentPropertiesRef();
	ANavigationData* NavData = NavSys ? NavSys->GetNavDataForProps(AgentProps, Character->GetActorLocation()) : nullptr;
	if (!NavData)
	{
		SetNavigationStatus(EYUFSNavigationStatus::Failed, EYUFSNavigationFailure::NoNavigationData);
		return;
	}

	FNavLocation ProjectedDestination;
	if (!NavSys->ProjectPointToNavigation(Destination, ProjectedDestination,
		FVector(100.f, 100.f, 200.f), NavData))
	{
		SetNavigationStatus(EYUFSNavigationStatus::Failed, EYUFSNavigationFailure::DestinationOffNavMesh);
		return;
	}
	CurrentDestination = ProjectedDestination.Location;
	FNavLocation ProjectedStart;
	// Start at the feet and use a tight projection: do not snap a trapped NPC onto another floor.
	if (!NavSys->ProjectPointToNavigation(GetOwnerFeetLocation(), ProjectedStart,
		FVector(60.f, 60.f, 100.f), NavData))
	{
		SetNavigationStatus(EYUFSNavigationStatus::Failed, EYUFSNavigationFailure::StartOffNavMesh);
		return;
	}

	QueryHazardSnapshot = GetPerceivedHazardSnapshot(Frame);
	HazardDataStatus = QueryHazardSnapshot.Status;
	if (HazardDataStatus != EYUFSHazardDataStatus::Ready)
	{
		const auto Status = HazardDataStatus == EYUFSHazardDataStatus::Loading
			? EYUFSNavigationStatus::WaitingForHazardData : EYUFSNavigationStatus::Failed;
		SetNavigationStatus(Status, EYUFSNavigationFailure::HazardDataUnavailable);
		return;
	}
	const auto Filter = UYUFSSmokeNavigationQueryFilter::CreateQueryFilter(
		*NavData, Character, QueryHazardSnapshot, GetHazardSettings());
	if (!Filter.IsValid())
	{
		SetNavigationStatus(EYUFSNavigationStatus::Failed, EYUFSNavigationFailure::UnsupportedNavData);
		return;
	}
	FPathFindingQuery Query(Character, *NavData, ProjectedStart.Location, CurrentDestination, Filter);
	Query.SetAllowPartialPaths(false);
	PendingNavigationSystem = NavSys;
	ActiveQueryId = NavSys->FindPathAsync(AgentProps, Query,
		FNavPathQueryDelegate::CreateUObject(this, &UYUFSSmokeAwareNavigator::OnPathFound, RequestGeneration));
	if (ActiveQueryId == INVALID_NAVQUERYID)
	{
		SetNavigationStatus(EYUFSNavigationStatus::Failed, EYUFSNavigationFailure::NoPath);
		return;
	}
	SetNavigationStatus(EYUFSNavigationStatus::Pathfinding);
}

void UYUFSSmokeAwareNavigator::OnPathFound(uint32 QueryId, ENavigationQueryResult::Type Result,
	FNavPathSharedPtr Path, uint32 Generation)
{
	if (Generation != RequestGeneration || QueryId != ActiveQueryId || !bIsPathfinding) return;
	ActiveQueryId = INVALID_NAVQUERYID;
	PendingNavigationSystem.Reset();
	if (Result != ENavigationQueryResult::Success || !Path.IsValid() ||
		!Path->IsValid() || Path->GetPathPoints().Num() < 2 || Path->IsPartial())
	{
		CurrentPath.Reset();
		StopOwnerMovement();
		SetNavigationStatus(EYUFSNavigationStatus::Failed,
			Path.IsValid() && Path->IsPartial() ? EYUFSNavigationFailure::PartialPath : EYUFSNavigationFailure::NoPath);
		return;
	}
	for (const FNavPathPoint& Point : Path->GetPathPoints()) CurrentPath.Add(Point.Location);
	// String pulling can cross a hazard within one large NavMesh polygon. Validate the final geometry too.
	FYUFSHazardSnapshot LatestSnapshot = QueryHazardSnapshot;
	int32 LatestFrame = RequestFrame;
	if (IsValid(LevelDataMgr))
	{
		LatestFrame = LevelDataMgr->GetCurrentHazardFrame();
		LatestSnapshot = GetPerceivedHazardSnapshot(LatestFrame);
		HazardDataStatus = LatestSnapshot.Status;
		if (LatestSnapshot.Status != EYUFSHazardDataStatus::Ready)
		{
			StartPathRequest(RequestedDestination, LatestFrame, EYUFSRepathReason::Smoke);
			return;
		}
	}
	LastPathScore = LatestSnapshot.ScorePath(CurrentPath, GetHazardSettings());
	if (LastPathScore.bUnsafeAhead)
	{
		if (LatestFrame != RequestFrame && !QueryHazardSnapshot.ScorePath(CurrentPath,GetHazardSettings()).bUnsafeAhead)
		{
			StartPathRequest(RequestedDestination, LatestFrame, EYUFSRepathReason::Smoke);
			return;
		}
		CurrentPath.Reset();
		StopOwnerMovement();
		SetNavigationStatus(EYUFSNavigationStatus::Failed, EYUFSNavigationFailure::UnsafePath);
		return;
	}
	CurrentWaypointIndex = 1;
	FailedPathRetries = 0;
	SetNavigationStatus(EYUFSNavigationStatus::Moving);
}

TArray<FVector> UYUFSSmokeAwareNavigator::BuildRemainingPath() const
{
	TArray<FVector> Remaining;
	if (GetOwner()) Remaining.Add(GetOwnerFeetLocation());
	for (int32 Index = CurrentWaypointIndex; Index < CurrentPath.Num(); ++Index)
	{
		Remaining.Add(CurrentPath[Index]);
	}
	return Remaining;
}

void UYUFSSmokeAwareNavigator::CheckAndReroute(int32 Frame)
{
	if (!IsFollowingPath() || !IsValid(LevelDataMgr) ||
		RerouteTimer < FMath::Max(0.1f, RerouteCheckInterval)) return;
	RerouteTimer = 0.f;
	// Include the segment from the NPC to its next waypoint, even on the final leg.
	const auto Snapshot = GetPerceivedHazardSnapshot(Frame);
	HazardDataStatus = Snapshot.Status;
	if (Snapshot.Status != EYUFSHazardDataStatus::Ready)
	{
		ReplanPath(Frame, EYUFSRepathReason::Smoke);
		return;
	}
	LastPathScore = Snapshot.ScorePath(BuildRemainingPath(), GetHazardSettings());
	// Compare the same remaining geometry in old/new data to avoid re-requesting an identical route every tick.
	const auto PreviousScore = QueryHazardSnapshot.ScorePath(BuildRemainingPath(), GetHazardSettings());
	if (LastPathScore.bUnsafeAhead || LastPathScore.MaxSmoke > FMath::Max(SmokeBlockThreshold, PreviousScore.MaxSmoke + 0.1f) ||
		LastPathScore.MaxHeat > FMath::Max(SmokeBlockThreshold, PreviousScore.MaxHeat + 0.1f))
		ReplanPath(Frame, EYUFSRepathReason::Smoke);
}

void UYUFSSmokeAwareNavigator::ClearPath()
{
	if (NavigationStatus == EYUFSNavigationStatus::Idle && ActiveQueryId == INVALID_NAVQUERYID) return;
	CancelPendingRequest();
	StopOwnerMovement();
	CurrentPath.Reset();
	CurrentWaypointIndex = 0;
	RequestedDestination = FVector::ZeroVector;
	CurrentDestination = FVector::ZeroVector;
	QueryHazardSnapshot = FYUFSHazardSnapshot();
	LastPathScore = FYUFSHazardPathScore();
	FailedPathRetries = 0;
	SetNavigationStatus(EYUFSNavigationStatus::Idle);
}

FVector UYUFSSmokeAwareNavigator::GetNextWaypoint() const
{
	return IsFollowingPath() && CurrentPath.IsValidIndex(CurrentWaypointIndex)
		? CurrentPath[CurrentWaypointIndex] : (GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector);
}

FVector UYUFSSmokeAwareNavigator::GetSteeringTarget(FVector ActorLocation, float LookAheadDistance) const
{
	if (!IsFollowingPath() || !CurrentPath.IsValidIndex(CurrentWaypointIndex)) return ActorLocation;
	FVector NextWaypoint = CurrentPath[CurrentWaypointIndex];
	NextWaypoint.Z = ActorLocation.Z;
	const float Distance = FVector::Dist2D(ActorLocation, NextWaypoint);
	if (Distance <= LookAheadDistance || Distance <= KINDA_SMALL_NUMBER) return NextWaypoint;
	return ActorLocation + (NextWaypoint - ActorLocation).GetSafeNormal2D() * FMath::Max(0.f, LookAheadDistance);
}

void UYUFSSmokeAwareNavigator::UpdateWaypoint(FVector ActorLocation, float AcceptanceRadius)
{
	if (!IsFollowingPath()) return;
	while (CurrentPath.IsValidIndex(CurrentWaypointIndex))
	{
		const FVector& Waypoint = CurrentPath[CurrentWaypointIndex];
		if (FVector::DistSquared2D(ActorLocation, Waypoint) > FMath::Square(AcceptanceRadius) ||
			FMath::Abs(ActorLocation.Z - Waypoint.Z) > WaypointHeightTolerance) break;
		++CurrentWaypointIndex;
	}
	if (CurrentWaypointIndex >= CurrentPath.Num())
	{
		StopOwnerMovement();
		SetNavigationStatus(EYUFSNavigationStatus::Arrived);
	}
}

FVector UYUFSSmokeAwareNavigator::GetOwnerFeetLocation() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	return Character ? Character->GetActorLocation() - FVector(0, 0, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())
		: (GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector);
}

FYUFSHazardSettings UYUFSSmokeAwareNavigator::GetHazardSettings() const
{
	FYUFSHazardSettings Settings;
	Settings.SmokeCost = SmokeTravelCost;
	Settings.HeatCost = HeatTravelCost;
	Settings.BlockSmoke = FMath::Clamp(UnsafeSmokeThreshold, 0.01f, 1.f);
	Settings.BlockHeat = FMath::Clamp(UnsafeHeatThreshold, 0.01f, 1.f);
	Settings.SampleHeightCm = HazardSampleHeightCm;
	return Settings;
}

FYUFSHazardSnapshot UYUFSSmokeAwareNavigator::GetPerceivedHazardSnapshot(int32 Frame) const
{
	auto Snapshot=IsValid(LevelDataMgr) ? LevelDataMgr->GetHazardSnapshot(Frame) : FYUFSHazardSnapshot();
	if (GetOwner()) if (auto* Perception=GetOwner()->FindComponentByClass<UYUFSNPCPerceptionComponent>())
		Snapshot=Perception->RestrictToKnowledge(Snapshot);
	return Snapshot;
}
bool UYUFSSmokeAwareNavigator::IsLocalRecoverySafe(const FVector& FromFeet,const FVector& ToFeet,int32 Frame) const
{
	const auto Snapshot=GetPerceivedHazardSnapshot(Frame);
	return Snapshot.Status==EYUFSHazardDataStatus::Ready && !Snapshot.ScorePath({FromFeet,ToFeet},GetHazardSettings()).bUnsafeAhead;
}
