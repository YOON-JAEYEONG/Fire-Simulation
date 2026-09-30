#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Integration/YUFSBelongingsRetrievalComponent.h"
#include "Props/YUFSBelongingsBag.h"
#include "Simulation/YUFSTimelineTypes.h"

namespace
{
struct FBelongingsWorldFixture
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	~FBelongingsWorldFixture() { World->DestroyWorld(false); }

	AActor* SpawnCarrier(const FVector& Location)
	{
		FActorSpawnParameters Spawn;
		Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Carrier = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location), Spawn);
		auto* Root = NewObject<USceneComponent>(Carrier, TEXT("Root"));
		Carrier->SetRootComponent(Root);
		Root->RegisterComponent();
		Carrier->SetActorLocation(Location);
		return Carrier;
	}
};
}

// Test-only access to the executor's phase (declared as a friend by the component).
struct FYUFSBelongingsRetrievalTestAccess
{
	static void SetPhase(UYUFSBelongingsRetrievalComponent& Retrieval, EYUFSBelongingsRetrievalPhase Phase)
	{
		Retrieval.Phase = Phase;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSBelongingsCarrySpeedTest,
	"YUFS.NPC.Belongings.CarryingSlowsTheOwner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSBelongingsCarrySpeedTest::RunTest(const FString&)
{
	FBelongingsWorldFixture F;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Npc = F.World->SpawnActor<AYUFSEvacuationNPC>(FVector(0, 0, 90), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("NPC spawns"), Npc)) return false;
	UYUFSBelongingsRetrievalComponent* Retrieval = Npc->GetBelongingsRetrievalComponent();
	if (!TestNotNull(TEXT("NPC has the belongings executor"), Retrieval)) return false;

	Npc->SetMovementSpeed(1000.f);
	const float Free = Npc->GetDesiredWalkingSpeed();
	TestTrue(TEXT("an owner without its bag is not slowed by it"), Free > Retrieval->CarryingMaxSpeedCmPerSec);
	FYUFSBelongingsRetrievalTestAccess::SetPhase(*Retrieval, EYUFSBelongingsRetrievalPhase::Carrying);
	TestEqual(TEXT("carrying the bag caps the owner's speed"), Npc->GetDesiredWalkingSpeed(),
		FMath::Min(Free, Retrieval->CarryingMaxSpeedCmPerSec));
	FYUFSBelongingsRetrievalTestAccess::SetPhase(*Retrieval, EYUFSBelongingsRetrievalPhase::None);
	TestEqual(TEXT("the cap ends with the carry"), Npc->GetDesiredWalkingSpeed(), Free);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSBelongingsBagCarryTest,
	"YUFS.NPC.Belongings.BagIsCarriedOnlyByItsOwner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSBelongingsBagCarryTest::RunTest(const FString&)
{
	FBelongingsWorldFixture F;
	AActor* Owner = F.SpawnCarrier(FVector(0, 0, 90));
	AActor* Stranger = F.SpawnCarrier(FVector(300, 0, 90));
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Bag = F.World->SpawnActor<AYUFSBelongingsBag>(FVector(100, 0, 20), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("bag spawns"), Bag)) return false;
	Bag->AssignOwnerNpc(Owner);

	TestEqual(TEXT("bag starts left behind"), Bag->GetBelongingsState(), EYUFSBelongingsState::LeftBehind);
	TestEqual(TEXT("bag never blocks pawns or sight"), Bag->Body->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	// A Static bag cannot be moved once play has begun, so it would stay on the floor in PIE.
	TestEqual(TEXT("bag body is movable"), Bag->Body->Mobility.GetValue(), EComponentMobility::Movable);
	TestEqual(TEXT("bag strap is movable"), Bag->Strap->Mobility.GetValue(), EComponentMobility::Movable);
	TestTrue(TEXT("pickup point is on the floor below the bag"),
		Bag->GetPickupLocation().Equals(FVector(100, 0, 0), 0.5f));
	TestFalse(TEXT("another NPC cannot take someone else's bag"), Bag->AttachToCarrier(Stranger));
	TestFalse(TEXT("refused pickup keeps the bag on the floor"), Bag->IsCarried());
	TestTrue(TEXT("owner picks up the bag"), Bag->AttachToCarrier(Owner));
	TestTrue(TEXT("bag is carried"), Bag->IsCarried());
	TestEqual(TEXT("bag rides on the owner's root"), Bag->GetAttachParentActor(), Owner);
	TestFalse(TEXT("a carried bag cannot be picked up twice"), Bag->AttachToCarrier(Owner));
	Owner->SetActorLocation(FVector(1000, 0, 90));
	TestTrue(TEXT("bag moves with its carrier"), FVector::Dist2D(Bag->GetActorLocation(), Owner->GetActorLocation()) < 60.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSLevelPlacedBagTest,
	"YUFS.NPC.Belongings.LevelPlacedBagIsClaimedAndRestored",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSLevelPlacedBagTest::RunTest(const FString&)
{
	FBelongingsWorldFixture F;
	AActor* Owner = F.SpawnCarrier(FVector(0, 0, 90));
	AActor* Other = F.SpawnCarrier(FVector(200, 0, 90));
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Bag = F.World->SpawnActor<AYUFSBelongingsBag>(FVector(100, 50, 20), FRotator(0, 30, 0), Spawn);
	if (!TestNotNull(TEXT("bag spawns"), Bag)) return false;
	TestTrue(TEXT("a bag without an owner actor counts as level-placed"), Bag->IsLevelPlaced());
	TestTrue(TEXT("first NPC claims the level bag"), Bag->Claim(Owner));
	TestFalse(TEXT("a claimed bag cannot be claimed again"), Bag->Claim(Other));
	TestTrue(TEXT("owner carries it"), Bag->AttachToCarrier(Owner));
	Owner->SetActorLocation(FVector(2000, 0, 90));
	Bag->ReleaseToHome();
	TestFalse(TEXT("released bag is on the floor again"), Bag->IsCarried());
	TestNull(TEXT("released bag is detached"), Bag->GetAttachParentActor());
	TestTrue(TEXT("released bag is back on the designer's spot"), Bag->GetActorLocation().Equals(FVector(100, 50, 20), 0.5f));
	TestFalse(TEXT("released bag has no owner"), Bag->IsClaimed());
	TestTrue(TEXT("another NPC can claim it next episode"), Bag->Claim(Other));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSBelongingsComponentContractTest,
	"YUFS.NPC.Belongings.RetrievalComponentContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSBelongingsComponentContractTest::RunTest(const FString&)
{
	FBelongingsWorldFixture F;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Npc = F.World->SpawnActor<AYUFSEvacuationNPC>(FVector(0, 0, 90), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("NPC spawns"), Npc)) return false;
	UYUFSBelongingsRetrievalComponent* Retrieval = Npc->GetBelongingsRetrievalComponent();
	if (!TestNotNull(TEXT("every evacuation NPC has the belongings executor"), Retrieval)) return false;
	TestEqual(TEXT("no retrieval before the episode"), Retrieval->GetPhase(), EYUFSBelongingsRetrievalPhase::None);
	TestFalse(TEXT("idle executor does not own movement"), Retrieval->IsActive());
	TestFalse(TEXT("idle executor holds no position"), Retrieval->Execute(0.1f));
	TestNull(TEXT("no bag is spawned before the NPC is live"), Retrieval->GetBag());

	const FYUFSBehaviorDecision Decision = Retrieval->MakeDecision();
	TestEqual(TEXT("published behavior is belongings retrieval"), Decision.Behavior, EYUFSHighLevelBehavior::RetrieveBelongings);
	TestEqual(TEXT("published task is gathering belongings"), Decision.DesiredTask, EYUFSActionTask::GatherBelongings);

	Retrieval->ResetForEpisode();
	TestEqual(TEXT("reset leaves no pending retrieval"), Retrieval->GetPhase(), EYUFSBelongingsRetrievalPhase::None);
	TestNull(TEXT("reset removes any bag"), Retrieval->GetBag());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSBelongingsTimelineReplayTest,
	"YUFS.NPC.Belongings.TimelineReplayShowsTheBag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSBelongingsTimelineReplayTest::RunTest(const FString&)
{
	FBelongingsWorldFixture F;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Npc = F.World->SpawnActor<AYUFSEvacuationNPC>(FVector(0, 0, 90), FRotator::ZeroRotator, Spawn);
	auto* Bag = F.World->SpawnActor<AYUFSBelongingsBag>(FVector(500, 0, 20), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("NPC spawns"), Npc) || !TestNotNull(TEXT("bag spawns"), Bag)) return false;
	UYUFSBelongingsRetrievalComponent* Retrieval = Npc->GetBelongingsRetrievalComponent();
	if (!TestNotNull(TEXT("NPC has the belongings executor"), Retrieval)) return false;

	// Recording side: the snapshot names this NPC's bag and where it is in the retrieval.
	Retrieval->PrepareForEpisode();
	if (!TestEqual(TEXT("NPC owns the nearby level bag"), Retrieval->GetBag(), Bag)) return false;
	const FYUFSTimelineNPCSnapshot OnFloor = Npc->BuildTimelineSnapshot();
	TestEqual(TEXT("snapshot names the bag"), OnFloor.BelongingsBag.Get(), Bag);
	TestEqual(TEXT("snapshot records the left-behind phase"), static_cast<int32>(OnFloor.BelongingsPhase),
		static_cast<int32>(EYUFSBelongingsRetrievalPhase::LeftBehind));

	// Replay side: frames as recorded later in the run, applied out of order like a scrubbed slider.
	FYUFSTimelineNPCSnapshot Carrying = OnFloor;
	Carrying.BelongingsPhase = static_cast<uint8>(EYUFSBelongingsRetrievalPhase::Carrying);
	FYUFSTimelineNPCSnapshot LeftBuilding = Carrying;
	LeftBuilding.bVisible = false;

	Retrieval->ApplyTimelineSnapshot(Carrying);
	TestEqual(TEXT("replay puts the bag on its owner's back"), Bag->GetAttachParentActor(), static_cast<AActor*>(Npc));
	TestFalse(TEXT("a carried bag is visible in the replay"), Bag->IsHidden());
	Npc->SetActorLocation(FVector(900, 0, 90));
	TestTrue(TEXT("the replayed bag moves with its owner"),
		FVector::Dist2D(Bag->GetActorLocation(), Npc->GetActorLocation()) < 60.f);
	Retrieval->ApplyTimelineSnapshot(LeftBuilding);
	TestTrue(TEXT("the bag leaves the building with its owner"), Bag->IsHidden());
	Retrieval->ApplyTimelineSnapshot(OnFloor);
	TestNull(TEXT("scrubbing back puts the bag down"), Bag->GetAttachParentActor());
	TestFalse(TEXT("the bag is visible on the floor again"), Bag->IsHidden());
	TestTrue(TEXT("the bag is back on its own spot"), Bag->GetActorLocation().Equals(FVector(500, 0, 20), 0.5f));
	TestFalse(TEXT("replay leaves the live state alone"), Bag->IsCarried());

	// Live: the owner walks out with the bag. It stays on the NPC's record for the replay.
	TestTrue(TEXT("owner picks up its bag"), Bag->AttachToCarrier(Npc));
	Retrieval->FinishEpisode();
	TestEqual(TEXT("the bag stays assigned for the replay"), Retrieval->GetBag(), Bag);
	TestTrue(TEXT("a carried bag leaves the building with its owner"), Bag->IsHidden());
	TestTrue(TEXT("nobody else can claim a bag that left the building"), Bag->IsCarried());

	// The next episode puts the designer's bag back, even though it was carried out last time.
	Retrieval->ResetForEpisode();
	TestNull(TEXT("next episode unassigns the bag"), Retrieval->GetBag());
	TestFalse(TEXT("next episode puts the bag down"), Bag->IsCarried());
	TestFalse(TEXT("next episode shows the bag again"), Bag->IsHidden());
	TestTrue(TEXT("next episode restores the designer's spot"), Bag->GetActorLocation().Equals(FVector(500, 0, 20), 0.5f));
	return true;
}
#endif
