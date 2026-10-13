#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Components/BoxComponent.h"
#include "Level/YUFSExitPoint.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Integration/YUFSExitTraversalComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSPhysicalExitTest, "YUFS.NPC.Navigation.ExitValidation.PhysicalOutwardTraversal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSPhysicalExitTest::RunTest(const FString&)
{
	const auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	auto* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
	if (!TestNotNull(TEXT("physics fixture"), World)) return false;
	FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* NPC = World->SpawnActor<AYUFSEvacuationNPC>(FVector(-100,0,90), FRotator::ZeroRotator, Spawn);
	auto* Exit = World->SpawnActor<AYUFSExitPoint>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
	Exit->ExitWidth = 180; Exit->ExitID = TEXT("OriginPortal");
	auto* Floor = World->SpawnActor<AActor>();
	auto* Box = NewObject<UBoxComponent>(Floor); Floor->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(500,500,10)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent();
	Floor->SetActorLocation(FVector(0,0,-10));
	const TArray<AYUFSExitPoint*> Exits {Exit};
	NPC->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	NPC->GetCharacterMovement()->bCanWalkOffLedges = true;
	auto* Tracker = NPC->ExitTraversal.Get();
	auto Sample = [&](FVector Location) { NPC->SetActorLocation(Location); Tracker->ObserveMovement(Exits, true, .5f); };
	Sample(FVector(-100,0,90)); Sample(FVector(100,0,90));
	TestFalse(TEXT("decorative portal is disabled until geometry is configured"), Tracker->HasExited());
	Exit->bPhysicalPassageEnabled = true;
	Tracker->Reset(); Sample(FVector(100,0,90)); Sample(FVector(150,0,90));
	TestFalse(TEXT("outside spawn does not evacuate"), Tracker->HasExited());
	Tracker->Reset(); Sample(FVector(100,0,90)); Sample(FVector(-100,0,90));
	TestFalse(TEXT("inward travel does not evacuate"), Tracker->HasExited());
	Tracker->Reset(); Sample(FVector(-100,0,440)); Sample(FVector(100,0,440));
	TestFalse(TEXT("other floor cannot trigger this portal"), Tracker->HasExited());
	Tracker->Reset(); Sample(FVector(-100,150,90)); Sample(FVector(100,150,90));
	TestFalse(TEXT("passing beside the opening cannot evacuate"), Tracker->HasExited());
	Tracker->Reset(); Sample(FVector(-100,0,90)); Tracker->DiscardMovementSample(); Sample(FVector(100,0,90));
	TestFalse(TEXT("explicit relocation never makes a crossing"), Tracker->HasExited());

	auto* Wall = World->SpawnActor<AActor>(); auto* WallBox = NewObject<UBoxComponent>(Wall); Wall->SetRootComponent(WallBox);
	WallBox->SetBoxExtent(FVector(10,100,110)); WallBox->SetCollisionProfileName(TEXT("BlockAll")); WallBox->RegisterComponent();
	Wall->SetActorLocation(FVector(0,0,110));
	Tracker->Reset(); Sample(FVector(-100,0,90)); Sample(FVector(100,0,90));
	TestFalse(TEXT("solid wall blocks a synthetic segment too"), Tracker->HasExited());
	WallBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Tracker->Reset(); Sample(FVector(-100,0,90)); Sample(FVector(5,0,90));
	TestFalse(TEXT("center crossed but rear of capsule is still inside"), Tracker->HasExited());
	Sample(FVector(100,0,90));
	TestTrue(TEXT("body physically cleared origin portal over multiple samples"), Tracker->HasExited());
	TestFalse(TEXT("exterior walking cannot walk off a ledge"), NPC->GetCharacterMovement()->bCanWalkOffLedges);
	TestEqual(TEXT("explicit stable exit identity"), Tracker->GetExitId(), FName(TEXT("OriginPortal")));
	const float Time = Tracker->GetExitTime(); Sample(FVector(150,0,90));
	TestEqual(TEXT("completion time is idempotent"), Tracker->GetExitTime(), Time);
	NPC->NotifyEpisodeFinished(EYUFSTerminalReason::ReachedExit);
	TestFalse(TEXT("exited NPC remains visible"), NPC->IsHidden());
	TestTrue(TEXT("exterior movement remains enabled"), NPC->GetCharacterMovement()->MovementMode != MOVE_None);
	const auto Snapshot = NPC->BuildTimelineSnapshot();
	TestTrue(TEXT("visible completed outcome is independently recorded"), Snapshot.bVisible && Snapshot.bEvacuated);
	NPC->SetActorLocation(FVector(100,0,90));
	TestTrue(TEXT("exterior step with full floor support is allowed"), Tracker->HasExteriorSupport(FVector(1,0,0),60));
	NPC->SetActorLocation(FVector(480,0,90));
	TestFalse(TEXT("exterior step over the apron edge is refused"), Tracker->HasExteriorSupport(FVector(1,0,0),60));
	Tracker->Reset(); Sample(FVector(-100,0,90)); Box->SetCollisionEnabled(ECollisionEnabled::NoCollision); Sample(FVector(100,0,90));
	TestFalse(TEXT("no exterior floor is not a valid exit"), Tracker->HasExited());
	TestTrue(TEXT("reset restores authored interior ledge behavior"), NPC->GetCharacterMovement()->bCanWalkOffLedges);
	Exit->ExteriorDistance = 300.f; Exit->ExteriorSpread = FVector2D(400,400);
	TestFalse(TEXT("different residents receive different exterior waiting positions"),
		Exit->GetExteriorWaitingDestination(1).Equals(Exit->GetExteriorWaitingDestination(2)));
	TestTrue(TEXT("waiting location is deterministic"),
		Exit->GetExteriorWaitingDestination(2).Equals(Exit->GetExteriorWaitingDestination(2)));
	World->DestroyWorld(false);
	return true;
}
#endif
