#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/CollisionProfile.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Fire/YUFSHazardField.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "NPC/Navigation/YUFSLocalMovementComponent.h"

// Nobody should stand still while evacuating: not in smoke with no smoke-free route, not behind a
// person who is just standing there, and not in stop-and-go behind someone walking the same way.

struct FYUFSSmokeEscapeTestAccess
{
	static void SetRoute(UYUFSSmokeAwareNavigator* Nav, const TArray<FVector>& Path, FVector Destination, const FYUFSHazardSnapshot& Snapshot)
	{
		Nav->CurrentPath = Path;
		Nav->RequestedDestination = Destination;
		Nav->LastPathScore = Snapshot.ScorePath(Path, Nav->GetHazardSettings());
	}
	static bool Try(UYUFSSmokeAwareNavigator* Nav, const FYUFSHazardSnapshot& Snapshot, float Now)
	{
		return Nav->TryAcceptSmokeEscape(Snapshot, Now);
	}
	static void Forget(UYUFSSmokeAwareNavigator* Nav) { Nav->SmokeRefusedSince = -1.f; Nav->SmokeEscapeOptions.Reset(); }
	static void Follow(UYUFSSmokeAwareNavigator* Nav, FVector From, FVector To)
	{
		Nav->CurrentPath = {From, To};
		Nav->CurrentWaypointIndex = 1;
		Nav->SetNavigationStatus(EYUFSNavigationStatus::Moving);
	}
};

struct FYUFSTrafficFlowTestAccess
{
	static FVector RecoveryTarget(const UYUFSLocalMovementComponent* Traffic) { return Traffic->RecoveryTarget; }
	static void Wait(UYUFSLocalMovementComponent* Traffic, AYUFSEvacuationNPC* For)
	{
		Traffic->YieldingTo = For;
		Traffic->State = EYUFSLocalMovementState::Yielding;
	}
};

namespace
{
	TSharedRef<FYUFSHazardGrid, ESPMode::ThreadSafe> SmokeBands(std::initializer_list<int32> Columns, bool bAlsoHeat)
	{
		auto Data = MakeShared<FYUFSHazardGrid, ESPMode::ThreadSafe>();
		Data->Dimensions = FIntVector(10, 3, 3); // 100 cm cells: x 0..1000, y 0..300, z 0..300
		Data->Density.Init(0, 90);
		Data->Temperature.Init(0, 90);
		for (const int32 X : Columns)
			for (int32 Y = 0; Y < 3; ++Y)
				for (int32 Z = 0; Z < 3; ++Z)
				{
					Data->Density[(X * 3 + Y) * 3 + Z] = 255;
					if (bAlsoHeat) Data->Temperature[(X * 3 + Y) * 3 + Z] = 255;
				}
		return Data;
	}
	FYUFSHazardSnapshot Field(TSharedRef<FYUFSHazardGrid, ESPMode::ThreadSafe> Data)
	{
		FYUFSHazardSnapshot Value;
		Value.Grid = Data;
		Value.GridToWorld = FTransform(FQuat::Identity, FVector::ZeroVector, FVector(100));
		Value.Status = EYUFSHazardDataStatus::Ready;
		Value.Frame = 0;
		return Value;
	}
	UWorld* PhysicsWorld()
	{
		const auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false)
			.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
		return UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
	}
	void Block(UWorld* World, FVector Centre, FVector Extent)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		auto* Box = NewObject<UBoxComponent>(Actor);
		Box->SetBoxExtent(Extent);
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Actor->SetRootComponent(Box);
		Box->RegisterComponent();
		Box->SetWorldLocation(Centre);
	}
	AYUFSEvacuationNPC* Person(UWorld* World, FVector Location)
	{
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AYUFSEvacuationNPC* Npc = World->SpawnActor<AYUFSEvacuationNPC>(Location, FRotator::ZeroRotator, Spawn);
		// Feet 2 cm above the floor top (z=0), whatever the capsule height.
		if (Npc) Npc->SetActorLocation(FVector(Location.X, Location.Y, Npc->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f));
		return Npc;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSSmokeEscapeTest, "YUFS.NPC.Navigation.Hazard.NoSmokeFreeRouteMeansCrossingTheLeastSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSSmokeEscapeTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AYUFSEvacuationNPC* Npc = Person(World, FVector(100, 150, 90));
	UYUFSSmokeAwareNavigator* Nav = Npc ? Npc->GetNavigator() : nullptr;
	if (!TestNotNull(TEXT("navigator"), Nav)) { World->DestroyWorld(false); return false; }
	const auto Smoke = Field(SmokeBands({4, 5, 7}, false));
	const TArray<FVector> Long{FVector(100, 150, 0), FVector(900, 150, 0)};
	const FVector LongExit(900, 150, 0);

	FYUFSSmokeEscapeTestAccess::SetRoute(Nav, Long, LongExit, Smoke);
	TestTrue(TEXT("the fixture route really is smoke-blocked"), Smoke.ScorePath(Long, FYUFSHazardSettings()).bUnsafeAhead);
	TestFalse(TEXT("a smoke-blocked route is refused at first"), FYUFSSmokeEscapeTestAccess::Try(Nav, Smoke, 10.f));
	TestFalse(TEXT("and while the smoke may still clear"), FYUFSSmokeEscapeTestAccess::Try(Nav, Smoke, 11.f));
	TestTrue(TEXT("then the route through the smoke is taken instead of standing still"), FYUFSSmokeEscapeTestAccess::Try(Nav, Smoke, 11.6f));

	FYUFSSmokeEscapeTestAccess::Forget(Nav);
	const auto Fire = Field(SmokeBands({4, 5, 7}, true));
	FYUFSSmokeEscapeTestAccess::SetRoute(Nav, Long, LongExit, Fire);
	TestFalse(TEXT("fire is never crossed"), FYUFSSmokeEscapeTestAccess::Try(Nav, Fire, 20.f));
	TestFalse(TEXT("however long the wait"), FYUFSSmokeEscapeTestAccess::Try(Nav, Fire, 40.f));

	// Standing in dense smoke already: leave almost at once.
	FYUFSSmokeEscapeTestAccess::Forget(Nav);
	Npc->SetActorLocation(FVector(450, 150, 90));
	FYUFSSmokeEscapeTestAccess::SetRoute(Nav, {FVector(450, 150, 0), FVector(900, 150, 0)}, LongExit, Smoke);
	TestFalse(TEXT("in dense smoke: not on the very first refusal"), FYUFSSmokeEscapeTestAccess::Try(Nav, Smoke, 50.f));
	TestTrue(TEXT("in dense smoke: out within a third of a second"), FYUFSSmokeEscapeTestAccess::Try(Nav, Smoke, 50.35f));

	// Two exits through smoke: the less smoky one is taken, and comparing never keeps anyone standing.
	FYUFSSmokeEscapeTestAccess::Forget(Nav);
	Npc->SetActorLocation(FVector(100, 150, 90));
	const TArray<FVector> Short{FVector(100, 150, 0), FVector(650, 150, 0)};
	const FVector ShortExit(650, 150, 0);
	FYUFSSmokeEscapeTestAccess::SetRoute(Nav, Long, LongExit, Smoke);
	FYUFSSmokeEscapeTestAccess::Try(Nav, Smoke, 0.f);
	FYUFSSmokeEscapeTestAccess::SetRoute(Nav, Short, ShortExit, Smoke);
	FYUFSSmokeEscapeTestAccess::Try(Nav, Smoke, 0.1f);
	FYUFSSmokeEscapeTestAccess::SetRoute(Nav, Long, LongExit, Smoke);
	TestFalse(TEXT("the smokier exit is refused while a less smoky one exists"), FYUFSSmokeEscapeTestAccess::Try(Nav, Smoke, 1.6f));
	FYUFSSmokeEscapeTestAccess::SetRoute(Nav, Short, ShortExit, Smoke);
	TestTrue(TEXT("the least smoky exit is taken"), FYUFSSmokeEscapeTestAccess::Try(Nav, Smoke, 1.7f));
	FYUFSSmokeEscapeTestAccess::SetRoute(Nav, Long, LongExit, Smoke);
	TestTrue(TEXT("after twice the wait any route through smoke is taken"), FYUFSSmokeEscapeTestAccess::Try(Nav, Smoke, 3.1f));

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSWalkAroundTest, "YUFS.NPC.Navigation.Traffic.WalksAroundSomeoneStandingInTheWay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSWalkAroundTest::RunTest(const FString&)
{
	UWorld* World = PhysicsWorld();
	if (!TestNotNull(TEXT("physics world"), World)) return false;
	Block(World, FVector(0, 0, -10), FVector(3000, 3000, 10)); // floor, top at z=0
	AYUFSEvacuationNPC* Walker = Person(World, FVector(0, 0, 90));
	AYUFSEvacuationNPC* Stander = Person(World, FVector(150, 0, 90));
	if (!Walker || !Stander) { AddError(TEXT("people missing")); World->DestroyWorld(false); return false; }
	const FVector Dir = Walker->GetLocalMovement()->ResolveDirection(FVector(1, 0, 0), 0.1f, 0);
	TestTrue(TEXT("the walker keeps walking"), Dir.Size() > 0.9f);
	TestTrue(TEXT("forward"), Dir.X > 0.5f);
	TestTrue(TEXT("and swings out to one side"), FMath::Abs(Dir.Y) > 0.3f);
	TestEqual(TEXT("walking around, not yielding"), Walker->GetLocalMovement()->GetState(), EYUFSLocalMovementState::Passing);

	// A narrow passage with no room beside the person: wait (and ask them to move) instead of squeezing.
	UWorld* Narrow = PhysicsWorld();
	Block(Narrow, FVector(0, 0, -10), FVector(3000, 3000, 10));
	Block(Narrow, FVector(0, 70, 150), FVector(1000, 10, 150));
	Block(Narrow, FVector(0, -70, 150), FVector(1000, 10, 150));
	AYUFSEvacuationNPC* Behind = Person(Narrow, FVector(0, 0, 90));
	AYUFSEvacuationNPC* InTheWay = Person(Narrow, FVector(150, 0, 90));
	if (!Behind || !InTheWay) { AddError(TEXT("narrow fixture missing")); Narrow->DestroyWorld(false); World->DestroyWorld(false); return false; }
	TestTrue(TEXT("no room to pass: the walker waits"),
		Behind->GetLocalMovement()->ResolveDirection(FVector(1, 0, 0), 0.6f, 0).IsNearlyZero());
	TestEqual(TEXT("yielding"), Behind->GetLocalMovement()->GetState(), EYUFSLocalMovementState::Yielding);
	TestTrue(TEXT("the person standing in the way is asked to move and steps on ahead"), InTheWay->GetLocalMovement()->IsMakingWay());
	TestTrue(TEXT("along the passage, clear of the walls"),
		FYUFSTrafficFlowTestAccess::RecoveryTarget(InTheWay->GetLocalMovement()).X > 200.f);

	Narrow->DestroyWorld(false);

	// On a stair (the person ahead a step up): no sidestep past them, it would end in the stairwell.
	UWorld* Stair = PhysicsWorld();
	Block(Stair, FVector(0, 0, -10), FVector(3000, 3000, 10));
	Block(Stair, FVector(300, 0, 15), FVector(200, 200, 15)); // a 30 cm step from x=100 on
	AYUFSEvacuationNPC* Below = Person(Stair, FVector(0, 0, 90));
	AYUFSEvacuationNPC* Above = Person(Stair, FVector(150, 0, 90));
	if (!Below || !Above) { AddError(TEXT("stair fixture missing")); Stair->DestroyWorld(false); World->DestroyWorld(false); return false; }
	Above->SetActorLocation(Above->GetActorLocation() + FVector(0, 0, 30));
	Below->GetLocalMovement()->ResolveDirection(FVector(1, 0, 0), 0.1f, 0);
	TestNotEqual(TEXT("no sidestep past someone on another step"), Below->GetLocalMovement()->GetState(), EYUFSLocalMovementState::Passing);
	Stair->DestroyWorld(false);

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSQueuePaceTest, "YUFS.NPC.Navigation.Traffic.KeepsThePaceOfSomeoneAheadInsteadOfStopAndGo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSQueuePaceTest::RunTest(const FString&)
{
	UWorld* World = PhysicsWorld();
	if (!TestNotNull(TEXT("physics world"), World)) return false;
	Block(World, FVector(0, 0, -10), FVector(3000, 3000, 10));
	AYUFSEvacuationNPC* Follower = Person(World, FVector(0, 0, 90));
	AYUFSEvacuationNPC* Leader = Person(World, FVector(120, 0, 90));
	if (!Follower || !Leader) { AddError(TEXT("people missing")); World->DestroyWorld(false); return false; }
	Follower->GetCharacterMovement()->MaxWalkSpeed = 400.f;
	FYUFSSmokeEscapeTestAccess::Follow(Leader->GetNavigator(), Leader->GetActorLocation(), Leader->GetActorLocation() + FVector(1000, 0, 0));
	Leader->GetCharacterMovement()->Velocity = FVector(300, 0, 0);
	const FVector Dir = Follower->GetLocalMovement()->ResolveDirection(FVector(1, 0, 0), 0.1f, 0);
	TestTrue(TEXT("walking on behind them, not stopping"), Dir.Size() > 0.3f);
	TestTrue(TEXT("at about their pace, slower than our own"), Dir.Size() < 0.9f);
	TestEqual(TEXT("following, not yielding"), Follower->GetLocalMovement()->GetState(), EYUFSLocalMovementState::Following);

	// When the person ahead is waiting in the queue themselves, the follower waits too (no cutting in).
	AYUFSEvacuationNPC* Front = Person(World, FVector(700, 0, 90));
	FYUFSTrafficFlowTestAccess::Wait(Leader->GetLocalMovement(), Front);
	Leader->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	TestTrue(TEXT("the queue stops when its front stops"),
		Follower->GetLocalMovement()->ResolveDirection(FVector(1, 0, 0), 0.1f, 0).IsNearlyZero());

	// Someone walking somewhere is not asked to step aside; they are walking already.
	TestFalse(TEXT("a walker does not step aside"), Leader->GetLocalMovement()->RequestMakeWay(Follower, FVector(1, 0, 0), 0));
	World->DestroyWorld(false);
	return true;
}

#endif
