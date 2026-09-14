#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "NavigationData.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "NPC/YUFSEvacuationNPC.h"

// Real navigator callback regression without relying on nondeterministic async timing.
struct FYUFSInteractionNavigationTestAccess
{
 UWorld* World = nullptr;
 ACharacter* Character = nullptr;
 UYUFSSmokeAwareNavigator* Navigator = nullptr;
 FYUFSInteractionNavigationTestAccess()
 {
  World = UWorld::CreateWorld(EWorldType::Game, false);
  Character = World->SpawnActor<ACharacter>();
  Navigator = NewObject<UYUFSSmokeAwareNavigator>(Character);
  Navigator->RegisterComponent();
 }
 ~FYUFSInteractionNavigationTestAccess() { Navigator->ClearPath(); World->DestroyWorld(false); }
 uint32 Begin(uint32 Id, FVector Destination)
 {
  Navigator->CancelPendingRequest(); Navigator->CurrentPath.Reset();
  Navigator->CurrentWaypointIndex = 0; Navigator->ActiveQueryId = Id;
  Navigator->RequestedDestination = Navigator->CurrentDestination = Destination;
  Navigator->LastAttemptDestination = Destination; Navigator->bHasAttempted = true;
  Navigator->SetStatus(EYUFSNavigationStatus::Pathfinding);
  return Navigator->RequestGeneration;
 }
 static FNavPathSharedPtr Path(TArray<FVector> Points, bool Partial = false)
 {
  auto Result = MakeShared<FNavigationPath, ESPMode::ThreadSafe>();
  for (auto Point : Points) Result->GetPathPoints().Emplace(Point);
  Result->SetIsPartial(Partial); Result->MarkReady(); return Result;
 }
 void Deliver(uint32 Id, uint32 Generation, FNavPathSharedPtr P)
 { Navigator->OnPathFound(Id, ENavigationQueryResult::Success, P, Generation); }
 TArray<FVector> Remaining() const { return Navigator->BuildRemainingPath(); }
 void AdvanceRetry() { Navigator->SinceAttempt = Navigator->FailedPathRetryInterval; }
 uint32 Generation() const { return Navigator->RequestGeneration; }
 void ExpireMemory() { for (auto& Known : Navigator->ObservedHazards) Known.ObservedAt = -100.f; }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSInteractionNavigationCancelTest,
 "YUFS.NPC.Navigation.Interaction.CancelAndLatestDestination",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSInteractionNavigationCancelTest::RunTest(const FString&)
{
 FYUFSInteractionNavigationTestAccess F;
 const FVector Fire(500, 0, 0), Exit(0, 500, 0);
 const uint32 First = F.Begin(11, Fire);
 F.Character->GetCharacterMovement()->Velocity = FVector(300, 0, 0);
 F.Navigator->ClearPath();
 F.Deliver(11, First, F.Path({FVector::ZeroVector, Fire}));
 TestTrue(TEXT("Cancelled suppression route cannot reappear"), F.Navigator->GetCurrentPathPoints().IsEmpty());
 TestTrue(TEXT("Cancellation consumes physical movement"), F.Character->GetVelocity().IsNearlyZero());
 TestFalse(TEXT("Cancellation clears pending flag"), F.Navigator->bIsPathfinding);
 const uint32 Old = F.Begin(12, Fire);
 const uint32 New = F.Begin(13, Exit);
 F.Deliver(12, Old, F.Path({FVector::ZeroVector, Fire}));
 TestTrue(TEXT("Late fire path cannot end newer evacuation request"), F.Navigator->bIsPathfinding);
 F.Deliver(13, New, F.Path({FVector::ZeroVector, Exit}));
 TestEqual(TEXT("Latest exact requested target is retained"), F.Navigator->GetRequestedDestination(), Exit);
 TestTrue(TEXT("Latest complete path is followed"), F.Navigator->IsFollowingPath());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSInteractionNavigationGeometryTest,
 "YUFS.NPC.Navigation.Interaction.CompletePathCornersAndFloors",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSInteractionNavigationGeometryTest::RunTest(const FString&)
{
 FYUFSInteractionNavigationTestAccess F;
 const FVector Corner(500, 0, 0), Goal(500, 500, 0);
 uint32 Gen = F.Begin(21, Goal);
 F.Deliver(21, Gen, F.Path({FVector::ZeroVector, Corner}, true));
 TestEqual(TEXT("Partial path is explicit failure"), F.Navigator->GetLastFailure(), EYUFSNavigationFailure::PartialPath);
 Gen = F.Begin(22, Goal); F.Deliver(22, Gen, F.Path({}));
 TestTrue(TEXT("Empty engine success cannot become moving"), !F.Navigator->IsFollowingPath());
 Gen = F.Begin(23, Goal); F.Deliver(23, Gen, F.Path({FVector::ZeroVector, Corner}));
 TestEqual(TEXT("Even unflagged short path is rejected"), F.Navigator->GetLastFailure(), EYUFSNavigationFailure::PartialPath);
 Gen = F.Begin(24, Goal); F.Deliver(24, Gen, F.Path({FVector::ZeroVector, Corner, Goal}));
 TestEqual(TEXT("Long lookahead cannot cut a building corner"), F.Navigator->GetSteeringTarget(FVector::ZeroVector, 2000.f), Corner);
 F.Navigator->UpdateWaypoint(Corner - FVector(60, 0, 0), 80.f);
 TestEqual(TEXT("Middle corner requires 25 cm, not 80 cm shortcut"), F.Navigator->GetCurrentWaypointIndex(), 1);
 F.Navigator->UpdateWaypoint(Corner, 80.f);
 F.Character->SetActorLocation(Corner + FVector(0, 0, F.Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
 TestEqual(TEXT("Final hazard segment includes feet to goal"), F.Remaining().Num(), 2);
 F.Navigator->UpdateWaypoint(Goal + FVector(0, 0, 400), 80.f);
 TestTrue(TEXT("Matching XY on another floor is not arrival"), F.Navigator->IsFollowingPath());
 F.Navigator->UpdateWaypoint(Goal, 80.f);
 TestEqual(TEXT("Physical arrival stops navigation"), F.Navigator->GetNavigationStatus(), EYUFSNavigationStatus::Arrived);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSInteractionNavigationKnowledgeTest,
 "YUFS.NPC.Navigation.Interaction.PersonalKnowledgeSnapshot",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSInteractionNavigationKnowledgeTest::RunTest(const FString&)
{
 FYUFSInteractionNavigationTestAccess A, B;
 const FVector Hot(250, 0, 120);
 const TArray<FVector> Route{FVector::ZeroVector, FVector(500, 0, 0)};
 TestTrue(TEXT("Unknown space has no inferred risk cost"), A.Navigator->IsKnownPathSafe(Route));
 A.Navigator->ReportObservedHazard(Hot, 0.f, 0.9f, 50.f);
 const auto Frozen = A.Navigator->GetObservedHazardSnapshot();
 TestTrue(TEXT("Observed heat blocks this NPC's path"), A.Navigator->IsKnownPathDangerous(Route));
 TestTrue(TEXT("Other NPC does not gain private knowledge"), B.Navigator->IsKnownPathSafe(Route));
 TestTrue(TEXT("Known heat adds positive query cost"), Frozen.SegmentAddedCost(Route[0], Route[1]) > 0.f);
 const TArray<FVector> Upstairs{Route[0] + FVector(0, 0, 300), Route[1] + FVector(0, 0, 300)};
 TestTrue(TEXT("One floor's observed patch is not projected upstairs"), A.Navigator->IsKnownPathSafe(Upstairs));
 A.Navigator->ReportObservedHazard(Hot, 0.f, 0.f);
 TestTrue(TEXT("Clear reobservation erases obsolete local hazard"), A.Navigator->IsKnownPathSafe(Route));
 TestFalse(TEXT("In-flight value snapshot is not changed by clear"), Frozen.IsPathSafe(Route));
 A.Navigator->ReportObservedHazard(Hot, 0.9f, 0.f);
 A.ExpireMemory();
 TestTrue(TEXT("Old observed patches expire"), A.Navigator->GetObservedHazardSnapshot().Samples.IsEmpty());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSInteractionNavigationRetryTest,
 "YUFS.NPC.Navigation.Interaction.MissingNavmeshBoundedRetry",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSInteractionNavigationRetryTest::RunTest(const FString&)
{
 FYUFSInteractionNavigationTestAccess F;
 F.Navigator->RequestPathAsync(FVector::ZeroVector, 0);
 TestEqual(TEXT("Origin is valid, absence of NavMesh is reported"), F.Navigator->GetLastFailure(), EYUFSNavigationFailure::NoNavigationData);
 const uint32 First = F.Generation();
 F.Navigator->RequestPathAsync(FVector::ZeroVector, 1);
 TestEqual(TEXT("Same failure cannot restart every Tick"), F.Generation(), First);
 for (int32 I = 0; I < F.Navigator->MaxFailedPathRetries; ++I)
 { F.AdvanceRetry(); F.Navigator->RequestPathAsync(FVector::ZeroVector, I + 2); }
 F.AdvanceRetry(); const uint32 Exhausted = F.Generation();
 F.Navigator->RequestPathAsync(FVector::ZeroVector, 50);
 TestEqual(TEXT("Retry budget is bounded even when FDS frame changes"), F.Generation(), Exhausted);
 F.Navigator->RequestPathAsync(FVector(0, 500, 0), 51);
 TestTrue(TEXT("A different target is accepted after a failed goal"), F.Generation() > Exhausted);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSInteractionNavigationPhysicalBlockTest,
 "YUFS.NPC.Navigation.Interaction.PhysicalBlockHasBoundedBudget",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSInteractionNavigationPhysicalBlockTest::RunTest(const FString&)
{
 FYUFSInteractionNavigationTestAccess F;
 const FVector Goal(500, 0, 0), Position = F.Character->GetActorLocation();
 for (int32 I = 0; I <= F.Navigator->MaxFailedPathRetries; ++I)
 {
  const uint32 Generation = F.Begin(50 + I, Goal);
  F.Deliver(50 + I, Generation, F.Path({FVector::ZeroVector, Goal}));
  F.Navigator->ReportMovementBlocked();
  TestEqual(TEXT("Physical block is an explicit failure"), F.Navigator->GetLastFailure(), EYUFSNavigationFailure::Blocked);
  TestEqual(TEXT("Blocking never teleports the owner"), F.Character->GetActorLocation(), Position);
  TestTrue(TEXT("Physical collision remains enabled"), F.Character->GetActorEnableCollision());
  F.AdvanceRetry();
 }
 const uint32 Exhausted = F.Generation();
 F.Navigator->RequestPathAsync(Goal, 1);
 TestEqual(TEXT("Identical successful NavMesh results cannot reset physical block budget"), F.Generation(), Exhausted);
 F.Navigator->RequestPathAsync(FVector(0, 500, 0), 1);
 TestTrue(TEXT("New destination can still be requested after blockage"), F.Generation() > Exhausted);
 return true;
}
struct FYUFSPersonalRetreatTestAccess
{
 static void Confirm(AYUFSEvacuationNPC* NPC, FVector Exit, const TArray<FVector>& Path)
 {
  NPC->bHasPreferredRetreat = true; NPC->PreferredRetreatExit = Exit;
  NPC->PreferredRetreatPath = Path; NPC->PreferredRetreatExpiresAt = 60.f;
 }
 static bool Preferred(AYUFSEvacuationNPC* NPC, FVector& Out) { return NPC->TryGetPreferredRetreat(Out); }
 static void Expire(AYUFSEvacuationNPC* NPC) { NPC->PreferredRetreatExpiresAt = -1.f; }
 static void Reset(AYUFSEvacuationNPC* NPC) { NPC->ResetRetreatKnowledge(); }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSPersonalRetreatHandoffTest,
 "YUFS.NPC.Navigation.Interaction.PreferredRetreatLifetimeAndObservedRisk",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSPersonalRetreatHandoffTest::RunTest(const FString&)
{
 UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
 auto* NPC = World->SpawnActor<AYUFSEvacuationNPC>();
 auto* Nav = NPC->GetNavigator();
 const FVector Exit(500, 0, 0);
 const TArray<FVector> Path{FVector::ZeroVector, Exit};
 FVector Chosen;
 FYUFSPersonalRetreatTestAccess::Confirm(NPC, Exit, Path);
 TestTrue(TEXT("Confirmed handoff does not require another synchronous search"), NPC->TryGetNearestKnownExit(Chosen));
 TestEqual(TEXT("Exact confirmed exit is retained rather than recomputing a geometric nearest exit"), Chosen, Exit);
 Nav->ReportObservedHazard(FVector(250, 0, 120), 0.f, 0.9f, 50.f);
 TestFalse(TEXT("New personally observed path hazard invalidates preferred retreat"), FYUFSPersonalRetreatTestAccess::Preferred(NPC, Chosen));
 Nav->ResetObservedHazards();
 FYUFSPersonalRetreatTestAccess::Confirm(NPC, Exit, Path);
 Nav->RequestPathAsync(Exit, 0); // No NavMesh: real failed request, no engine callback fabricated.
 TestFalse(TEXT("A failed retreat request cannot remain the permanent preferred exit"), FYUFSPersonalRetreatTestAccess::Preferred(NPC, Chosen));
 Nav->ClearPath();
 FYUFSPersonalRetreatTestAccess::Confirm(NPC, Exit, Path);
 Nav->RequestPathAsync(FVector(0, 500, 0), 1);
 TestFalse(TEXT("An explicit different destination invalidates stale retreat preference"), FYUFSPersonalRetreatTestAccess::Preferred(NPC, Chosen));
 Nav->ClearPath();
 FYUFSPersonalRetreatTestAccess::Confirm(NPC, Exit, Path);
 FYUFSPersonalRetreatTestAccess::Expire(NPC);
 TestFalse(TEXT("Handoff lifetime is bounded"), FYUFSPersonalRetreatTestAccess::Preferred(NPC, Chosen));
 FYUFSPersonalRetreatTestAccess::Confirm(NPC, Exit, Path);
 FYUFSPersonalRetreatTestAccess::Reset(NPC);
 TestFalse(TEXT("New episode clears prior retreat preference"), FYUFSPersonalRetreatTestAccess::Preferred(NPC, Chosen));
 World->DestroyWorld(false);
 return true;
}
#endif
