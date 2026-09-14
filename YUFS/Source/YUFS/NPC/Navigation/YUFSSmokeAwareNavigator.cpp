#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationData.h"

UYUFSSmokeAwareNavigator::UYUFSSmokeAwareNavigator() { PrimaryComponentTick.bCanEverTick = true; }
void UYUFSSmokeAwareNavigator::EndPlay(const EEndPlayReason::Type Reason)
{
 CancelPendingRequest();
 Super::EndPlay(Reason);
}
void UYUFSSmokeAwareNavigator::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
 Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
 SinceAttempt += DeltaTime;
 RerouteTimer += DeltaTime;
}
void UYUFSSmokeAwareNavigator::StopOwnerMovement()
{
 if (auto* Character = Cast<ACharacter>(GetOwner()))
 {
  Character->ConsumeMovementInputVector();
  if (auto* Movement = Character->GetCharacterMovement()) Movement->StopMovementImmediately();
 }
}
void UYUFSSmokeAwareNavigator::CancelPendingRequest()
{
 ++RequestGeneration; // Abort cannot stop an already running callback; generation can.
 if (ActiveQueryId != INVALID_NAVQUERYID && PendingNavigationSystem.IsValid())
  PendingNavigationSystem->AbortAsyncFindPathRequest(ActiveQueryId);
 ActiveQueryId = INVALID_NAVQUERYID;
 PendingNavigationSystem.Reset();
 bIsPathfinding = false;
}
void UYUFSSmokeAwareNavigator::SetStatus(EYUFSNavigationStatus NewStatus, EYUFSNavigationFailure Failure)
{
 Status = NewStatus;
 LastFailure = Failure;
 bIsPathfinding = Status == EYUFSNavigationStatus::Pathfinding;
 if (Status == EYUFSNavigationStatus::Failed) bLastAttemptFailed = true;
 else if (Status == EYUFSNavigationStatus::Moving || Status == EYUFSNavigationStatus::Arrived) bLastAttemptFailed = false;
 UE_LOG(LogTemp, Log, TEXT("[YUFS][Nav] agent=%s request=%u status=%s failure=%s target=%s frame=%d observed=%d"),
  *GetNameSafe(GetOwner()), RequestGeneration,
  *StaticEnum<EYUFSNavigationStatus>()->GetNameStringByValue(int64(Status)),
  *StaticEnum<EYUFSNavigationFailure>()->GetNameStringByValue(int64(Failure)),
  *RequestedDestination.ToCompactString(), RequestFrame, QueryKnowledge.Samples.Num());
}
void UYUFSSmokeAwareNavigator::RequestPathAsync(FVector Destination, int32 Frame)
{
 const bool SameGoal = bHasAttempted && LastAttemptDestination.Equals(Destination, 5.f);
 if (SameGoal && (bIsPathfinding || IsFollowingPath())) return;
 // Caller ClearPath/RequestPath loops cannot defeat the failure cooldown.
 if (SameGoal && bLastAttemptFailed && AttemptKnowledgeRevision == KnowledgeRevision)
 {
  if (SinceAttempt < FMath::Max(0.1f, FailedPathRetryInterval) || RetryCount >= FMath::Max(0, MaxFailedPathRetries)) return;
  ++RetryCount;
 }
 else RetryCount = 0;
 StartPathRequest(Destination, Frame);
}
void UYUFSSmokeAwareNavigator::StartPathRequest(FVector Destination, int32 Frame)
{
 CancelPendingRequest();
 StopOwnerMovement();
 CurrentPath.Reset(); CurrentWaypointIndex = 0;
 RequestedDestination = CurrentDestination = LastAttemptDestination = Destination;
 bHasAttempted = true; RequestFrame = Frame; SinceAttempt = RerouteTimer = 0.f;
 AttemptKnowledgeRevision = KnowledgeRevision;
 QueryKnowledge = GetObservedHazardSnapshot();
 auto* Character = Cast<ACharacter>(GetOwner());
 if (!Character) { SetStatus(EYUFSNavigationStatus::Failed, EYUFSNavigationFailure::InvalidOwner); return; }
 if (Destination.ContainsNaN()) { SetStatus(EYUFSNavigationStatus::Failed, EYUFSNavigationFailure::InvalidDestination); return; }
 auto* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
 const auto& AgentProps = Character->GetNavAgentPropertiesRef();
 auto* NavData = NavSys ? NavSys->GetNavDataForProps(AgentProps, GetOwnerFeetLocation()) : nullptr;
 if (!NavData) { SetStatus(EYUFSNavigationStatus::Failed, EYUFSNavigationFailure::NoNavigationData); return; }
 FNavLocation End, Start;
 if (!NavSys->ProjectPointToNavigation(Destination, End, FVector(100, 100, 150), NavData)
  || FMath::Abs(End.Location.Z - Destination.Z) > 150.f
  || FVector::DistSquared2D(End.Location, Destination) > FMath::Square(100.f))
 { SetStatus(EYUFSNavigationStatus::Failed, EYUFSNavigationFailure::DestinationOffNavMesh); return; }
 const FVector Feet = GetOwnerFeetLocation();
 if (!NavSys->ProjectPointToNavigation(Feet, Start, FVector(60, 60, 80), NavData)
  || FMath::Abs(Start.Location.Z - Feet.Z) > 80.f)
 { SetStatus(EYUFSNavigationStatus::Failed, EYUFSNavigationFailure::StartOffNavMesh); return; }
 CurrentDestination = End.Location;
 const auto Filter = UYUFSSmokeNavigationQueryFilter::CreateQueryFilter(*NavData, Character, QueryKnowledge);
 if (!Filter.IsValid()) { SetStatus(EYUFSNavigationStatus::Failed, EYUFSNavigationFailure::UnsupportedNavData); return; }
 FPathFindingQuery Query(Character, *NavData, Start.Location, End.Location, Filter);
 Query.SetAllowPartialPaths(false);
 PendingNavigationSystem = NavSys;
 ActiveQueryId = NavSys->FindPathAsync(AgentProps, Query,
  FNavPathQueryDelegate::CreateUObject(this, &UYUFSSmokeAwareNavigator::OnPathFound, RequestGeneration));
 if (ActiveQueryId == INVALID_NAVQUERYID) { SetStatus(EYUFSNavigationStatus::Failed, EYUFSNavigationFailure::NoPath); return; }
 SetStatus(EYUFSNavigationStatus::Pathfinding);
}
void UYUFSSmokeAwareNavigator::OnPathFound(uint32 QueryId, ENavigationQueryResult::Type Result,
 FNavPathSharedPtr Path, uint32 Generation)
{
 if (Generation != RequestGeneration || QueryId != ActiveQueryId || !bIsPathfinding) return;
 ActiveQueryId = INVALID_NAVQUERYID; PendingNavigationSystem.Reset();
 if (Result != ENavigationQueryResult::Success || !Path.IsValid() || !Path->IsValid()
  || Path->GetPathPoints().Num() < 2 || Path->IsPartial())
 {
  CurrentPath.Reset(); StopOwnerMovement();
  SetStatus(EYUFSNavigationStatus::Failed, Path.IsValid() && Path->IsPartial()
   ? EYUFSNavigationFailure::PartialPath : EYUFSNavigationFailure::NoPath);
  return;
 }
 for (const auto& Point : Path->GetPathPoints())
 {
  if (Point.Location.ContainsNaN())
  { CurrentPath.Reset(); StopOwnerMovement(); SetStatus(EYUFSNavigationStatus::Failed, EYUFSNavigationFailure::NoPath); return; }
  CurrentPath.Add(Point.Location);
 }
 if (!CurrentPath.Last().Equals(CurrentDestination, 50.f))
 { CurrentPath.Reset(); StopOwnerMovement(); SetStatus(EYUFSNavigationStatus::Failed, EYUFSNavigationFailure::PartialPath); return; }
 // Final geometry uses the newest personal observations, never global FDS truth.
 if (!GetObservedHazardSnapshot().IsPathSafe(CurrentPath))
 { CurrentPath.Reset(); StopOwnerMovement(); SetStatus(EYUFSNavigationStatus::Failed, EYUFSNavigationFailure::UnsafeKnownPath); return; }
 CurrentWaypointIndex = 1; RetryCount = 0;
 SetStatus(EYUFSNavigationStatus::Moving);
}
void UYUFSSmokeAwareNavigator::CheckAndReroute(int32 Frame)
{
 if (!IsFollowingPath() || RerouteTimer < FMath::Max(0.1f, RerouteCheckInterval)) return;
 RerouteTimer = 0.f;
 const auto Remaining = BuildRemainingPath();
 const auto Latest = GetObservedHazardSnapshot();
 float PreviousCost = 0.f, CurrentCost = 0.f;
 for (int32 I = 1; I < Remaining.Num(); ++I)
 {
  PreviousCost += QueryKnowledge.SegmentAddedCost(Remaining[I - 1], Remaining[I]);
  CurrentCost += Latest.SegmentAddedCost(Remaining[I - 1], Remaining[I]);
 }
 if (!Latest.IsPathSafe(Remaining) || CurrentCost > PreviousCost + 100.f)
  StartPathRequest(RequestedDestination, Frame);
}
void UYUFSSmokeAwareNavigator::ClearPath()
{
 CancelPendingRequest(); StopOwnerMovement();
 CurrentPath.Reset(); CurrentWaypointIndex = 0;
 RequestedDestination = CurrentDestination = FVector::ZeroVector;
 QueryKnowledge = {};
 // Keep retry bookkeeping so repeated clears do not spin on an unreachable goal.
 if (Status != EYUFSNavigationStatus::Idle) SetStatus(EYUFSNavigationStatus::Idle);
}
void UYUFSSmokeAwareNavigator::ReportMovementBlocked()
{
 const FVector Position = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
 const bool SameBlock = RequestedDestination.Equals(LastBlockedGoal, 5.f)
  && FVector::DistSquared2D(Position, LastBlockedPosition) < FMath::Square(150.f)
  && LastBlockedKnowledgeRevision == KnowledgeRevision;
 RepeatedMovementBlocks = SameBlock ? RepeatedMovementBlocks + 1 : 1;
 LastBlockedPosition = Position; LastBlockedGoal = RequestedDestination;
 LastBlockedKnowledgeRevision = KnowledgeRevision;
 CancelPendingRequest(); StopOwnerMovement();
 CurrentPath.Reset(); CurrentWaypointIndex = 0; SinceAttempt = 0.f;
 // A geometrically valid but physically blocked route cannot reset its own
 // recovery budget by returning the same successful NavMesh path repeatedly.
 RetryCount = RepeatedMovementBlocks > FMath::Max(0, MaxFailedPathRetries)
  ? FMath::Max(0, MaxFailedPathRetries) : 0;
 SetStatus(EYUFSNavigationStatus::Failed, EYUFSNavigationFailure::Blocked);
}
FVector UYUFSSmokeAwareNavigator::GetNextWaypoint() const
{
 return IsFollowingPath() && CurrentPath.IsValidIndex(CurrentWaypointIndex)
  ? CurrentPath[CurrentWaypointIndex] : (GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector);
}
FVector UYUFSSmokeAwareNavigator::GetSteeringTarget(FVector ActorLocation, float LookAheadDistance) const
{
 if (!IsFollowingPath() || !CurrentPath.IsValidIndex(CurrentWaypointIndex)) return ActorLocation;
 FVector Next = CurrentPath[CurrentWaypointIndex]; Next.Z = ActorLocation.Z;
 const float Distance = FVector::Dist2D(ActorLocation, Next);
 return Distance <= LookAheadDistance ? Next
  : ActorLocation + (Next - ActorLocation).GetSafeNormal2D() * FMath::Max(0.f, LookAheadDistance);
}
void UYUFSSmokeAwareNavigator::UpdateWaypoint(FVector ActorLocation, float AcceptanceRadius)
{
 if (!IsFollowingPath()) return;
 while (CurrentPath.IsValidIndex(CurrentWaypointIndex))
 {
  const auto& Point = CurrentPath[CurrentWaypointIndex];
  const float Radius = CurrentWaypointIndex + 1 < CurrentPath.Num() ? FMath::Min(AcceptanceRadius, 25.f) : AcceptanceRadius;
  if (FVector::DistSquared2D(ActorLocation, Point) > FMath::Square(Radius)
   || FMath::Abs(ActorLocation.Z - Point.Z) > WaypointHeightTolerance) break;
  ++CurrentWaypointIndex;
 }
 if (CurrentWaypointIndex >= CurrentPath.Num()) { StopOwnerMovement(); SetStatus(EYUFSNavigationStatus::Arrived); }
}
FVector UYUFSSmokeAwareNavigator::GetOwnerFeetLocation() const
{
 const auto* Character = Cast<ACharacter>(GetOwner());
 return Character ? Character->GetActorLocation() - FVector(0, 0, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())
  : (GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector);
}
TArray<FVector> UYUFSSmokeAwareNavigator::BuildRemainingPath() const
{
 TArray<FVector> Result; Result.Add(GetOwnerFeetLocation());
 for (int32 I = CurrentWaypointIndex; I < CurrentPath.Num(); ++I) Result.Add(CurrentPath[I]);
 return Result;
}
void UYUFSSmokeAwareNavigator::ReportObservedHazard(FVector Position, float Smoke, float Heat, float Radius)
{
 if (Position.ContainsNaN() || !FMath::IsFinite(Smoke) || !FMath::IsFinite(Heat) || !FMath::IsFinite(Radius)) return;
 const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
 const int32 Removed = ObservedHazards.RemoveAll([&](const auto& K) { return Now - K.ObservedAt > FMath::Max(1.f, HazardMemorySeconds); });
 if (Removed) ++KnowledgeRevision;
 const int32 Existing = ObservedHazards.IndexOfByPredicate([&](const auto& K) { return K.Location.Equals(Position, 10.f); });
 Smoke = FMath::Clamp(Smoke, 0.f, 1.f); Heat = FMath::Clamp(Heat, 0.f, 1.f);
 if (Smoke <= 0.01f && Heat <= 0.01f)
 {
  if (Existing != INDEX_NONE) { ObservedHazards.RemoveAt(Existing); ++KnowledgeRevision; }
  return;
 }
 FYUFSObservedHazard NewSample{Position, Smoke, Heat, FMath::Clamp(Radius, 10.f, 100.f), Now};
 if (Existing != INDEX_NONE)
 {
  const auto& Old = ObservedHazards[Existing];
  if (!FMath::IsNearlyEqual(Old.Smoke, Smoke, 0.02f) || !FMath::IsNearlyEqual(Old.Heat, Heat, 0.02f)) ++KnowledgeRevision;
  ObservedHazards[Existing] = NewSample;
 }
 else
 {
  if (ObservedHazards.Num() >= 256)
  {
   int32 Oldest = 0;
   for (int32 I = 1; I < ObservedHazards.Num(); ++I)
    if (ObservedHazards[I].ObservedAt < ObservedHazards[Oldest].ObservedAt) Oldest = I;
   ObservedHazards.RemoveAt(Oldest);
  }
  ObservedHazards.Add(NewSample); ++KnowledgeRevision;
 }
}
void UYUFSSmokeAwareNavigator::ResetObservedHazards()
{
 ObservedHazards.Reset(); ++KnowledgeRevision;
}
FYUFSObservedHazardSnapshot UYUFSSmokeAwareNavigator::GetObservedHazardSnapshot() const
{
 FYUFSObservedHazardSnapshot Result;
 Result.BlockSmoke = FMath::Clamp(UnsafeSmokeThreshold, 0.01f, 1.f);
 Result.BlockHeat = FMath::Clamp(UnsafeHeatThreshold, 0.01f, 1.f);
 const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
 for (const auto& Known : ObservedHazards)
  if (Now - Known.ObservedAt <= FMath::Max(1.f, HazardMemorySeconds)) Result.Samples.Add(Known);
 return Result;
}

