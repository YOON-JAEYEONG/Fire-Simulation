#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Integration/YUFSBelongingsRetrievalComponent.h"
#include "Props/YUFSBelongingsBag.h"

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
#endif
