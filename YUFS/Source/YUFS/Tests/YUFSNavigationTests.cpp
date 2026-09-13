#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "NavigationData.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"

// Exercise real component callbacks with controlled delivery order. These tests
// do not claim to test a baked level NavMesh or fire avoidance in a live map.
struct FYUFSNavigationTestAccess
{
	UWorld* World;
	ACharacter* Character;
	UYUFSSmokeAwareNavigator* Navigator;

	FYUFSNavigationTestAccess()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false);
		Character = World->SpawnActor<ACharacter>();
		Navigator = NewObject<UYUFSSmokeAwareNavigator>(Character);
		Navigator->RegisterComponent();
	}
	~FYUFSNavigationTestAccess()
	{
		Navigator->ClearPath();
		World->DestroyWorld(false);
	}
	uint32 BeginRequest(uint32 QueryId, FVector Destination)
	{
		Navigator->CancelPendingRequest();
		Navigator->CurrentPath.Reset();
		Navigator->CurrentWaypointIndex = 0;
		Navigator->ActiveQueryId = QueryId;
		Navigator->RequestedDestination = Destination;
		Navigator->CurrentDestination = Destination;
		Navigator->SetNavigationStatus(EYUFSNavigationStatus::Pathfinding);
		return Navigator->RequestGeneration;
	}
	void Deliver(uint32 QueryId, uint32 Generation, FNavPathSharedPtr Path,
		ENavigationQueryResult::Type Result = ENavigationQueryResult::Success)
	{
		Navigator->OnPathFound(QueryId, Result, Path, Generation);
	}
	static FNavPathSharedPtr MakePath(const TArray<FVector>& Points, bool bPartial = false)
	{
		FNavPathSharedPtr Path = MakeShared<FNavigationPath, ESPMode::ThreadSafe>();
		for (const FVector& Point : Points) Path->GetPathPoints().Emplace(Point);
		Path->SetIsPartial(bPartial);
		Path->MarkReady();
		return Path;
	}
	TArray<FVector> RemainingPath() const { return Navigator->BuildRemainingPath(); }
	void AdvanceRetryClock() { Navigator->TimeSinceRequest = Navigator->FailedPathRetryInterval; }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSNavigationCancelTest,
	"YUFS.NPC.Navigation.CancelIgnoresLateResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSNavigationCancelTest::RunTest(const FString& Parameters)
{
	FYUFSNavigationTestAccess Fixture;
	const uint32 Generation = Fixture.BeginRequest(11, FVector(500.f, 0.f, 0.f));
	Fixture.Character->GetCharacterMovement()->Velocity = FVector(300.f, 0.f, 0.f);
	Fixture.Navigator->ClearPath();
	Fixture.Deliver(11, Generation, Fixture.MakePath({FVector::ZeroVector, FVector(500.f, 0.f, 0.f)}));
	TestEqual(TEXT("Cancellation survives a late successful callback"), Fixture.Navigator->GetNavigationStatus(), EYUFSNavigationStatus::Idle);
	TestTrue(TEXT("No path reappears after cancellation"), Fixture.Navigator->GetCurrentPathPoints().IsEmpty());
	TestFalse(TEXT("Cancelled request is not pending"), Fixture.Navigator->bIsPathfinding);
	TestTrue(TEXT("Cancellation stops movement"), Fixture.Character->GetVelocity().IsNearlyZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSNavigationReplacementTest,
	"YUFS.NPC.Navigation.NewDestinationWins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSNavigationReplacementTest::RunTest(const FString& Parameters)
{
	FYUFSNavigationTestAccess Fixture;
	const uint32 OldGeneration = Fixture.BeginRequest(11, FVector(500.f, 0.f, 0.f));
	const FVector NewDestination(0.f, 500.f, 0.f);
	const uint32 NewGeneration = Fixture.BeginRequest(12, NewDestination);
	Fixture.Deliver(11, OldGeneration, nullptr, ENavigationQueryResult::Fail);
	TestTrue(TEXT("Old failure cannot end the new request"), Fixture.Navigator->bIsPathfinding);
	Fixture.Deliver(12, NewGeneration, Fixture.MakePath({FVector::ZeroVector, NewDestination}));
	TestEqual(TEXT("Latest destination is retained"), Fixture.Navigator->GetCurrentDestination(), NewDestination);
	TestTrue(TEXT("Latest complete path can be followed"), Fixture.Navigator->IsFollowingPath());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSNavigationPartialPathTest,
	"YUFS.NPC.Navigation.RejectsPartialPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSNavigationPartialPathTest::RunTest(const FString& Parameters)
{
	FYUFSNavigationTestAccess Fixture;
	const uint32 Generation = Fixture.BeginRequest(11, FVector(500.f, 0.f, 0.f));
	Fixture.Deliver(11, Generation,
		Fixture.MakePath({FVector::ZeroVector, FVector(200.f, 0.f, 0.f)}, true));
	TestEqual(TEXT("A path ending before the destination is a failure"), Fixture.Navigator->GetNavigationStatus(), EYUFSNavigationStatus::Failed);
	TestEqual(TEXT("Failure explains why the destination is unreachable"), Fixture.Navigator->GetLastFailure(), EYUFSNavigationFailure::PartialPath);
	TestTrue(TEXT("NPC must not follow the incomplete path"), Fixture.Navigator->GetCurrentPathPoints().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSNavigationGeometryTest,
	"YUFS.NPC.Navigation.CornersFinalSegmentAndArrival",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSNavigationGeometryTest::RunTest(const FString& Parameters)
{
	FYUFSNavigationTestAccess Fixture;
	const FVector Corner(500.f, 0.f, 0.f);
	const FVector Destination(500.f, 500.f, 0.f);
	const uint32 Generation = Fixture.BeginRequest(11, Destination);
	Fixture.Deliver(11, Generation, Fixture.MakePath({FVector::ZeroVector, Corner, Destination}));
	TestEqual(TEXT("Long lookahead cannot cut across a corner"),
		Fixture.Navigator->GetSteeringTarget(FVector::ZeroVector, 1000.f), Corner);
	Fixture.Navigator->UpdateWaypoint(Corner, 80.f);
	Fixture.Character->SetActorLocation(Corner);
	const TArray<FVector> Remaining = Fixture.RemainingPath();
	TestEqual(TEXT("Final leg still has two points for hazard sampling"), Remaining.Num(), 2);
	if (Remaining.Num() == 2)
	{
		TestEqual(TEXT("Hazard sampling starts at the NPC's walking surface"), Remaining[0],
			Corner - FVector(0, 0, Fixture.Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
		TestEqual(TEXT("Hazard sampling reaches the destination"), Remaining[1], Destination);
	}
	Fixture.Navigator->UpdateWaypoint(Destination + FVector(0.f, 0.f, 400.f), 80.f);
	TestTrue(TEXT("Same XY on another floor is not arrival"), Fixture.Navigator->IsFollowingPath());
	Fixture.Navigator->UpdateWaypoint(Destination, 80.f);
	TestEqual(TEXT("Reaching the final waypoint reports arrival"), Fixture.Navigator->GetNavigationStatus(), EYUFSNavigationStatus::Arrived);
	TestEqual(TEXT("Arrived NPC receives no further steering"),
		Fixture.Navigator->GetSteeringTarget(Destination), Destination);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSNavigationFailureRetryTest,
	"YUFS.NPC.Navigation.MissingNavMeshAndBoundedRetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSNavigationFailureRetryTest::RunTest(const FString& Parameters)
{
	FYUFSNavigationTestAccess Fixture;
	// Origin is a valid input: it must reach the NavMesh check, not be ignored.
	Fixture.Navigator->RequestPathAsync(FVector::ZeroVector, 0);
	TestEqual(TEXT("Missing navigation reports failure"), Fixture.Navigator->GetNavigationStatus(), EYUFSNavigationStatus::Failed);
	TestEqual(TEXT("Failure identifies missing navigation data"), Fixture.Navigator->GetLastFailure(), EYUFSNavigationFailure::NoNavigationData);
	TestFalse(TEXT("Failed request cannot retry every tick"), Fixture.Navigator->ShouldRetryPath());
	for (int32 Retry = 0; Retry < Fixture.Navigator->MaxFailedPathRetries; ++Retry)
	{
		Fixture.AdvanceRetryClock();
		TestTrue(TEXT("Retry becomes available after cooldown"), Fixture.Navigator->ShouldRetryPath());
		Fixture.Navigator->ReplanPath(Retry + 1, EYUFSRepathReason::Retry);
	}
	Fixture.AdvanceRetryClock();
	TestFalse(TEXT("An unreachable goal has a bounded retry budget"), Fixture.Navigator->ShouldRetryPath());
	return true;
}

#endif
