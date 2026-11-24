#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Fire/YUFSInteractionDoor.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Navigation/YUFSExitKnowledgeComponent.h"
#include "NPC/Navigation/YUFSLocalMovementComponent.h"
#include "NPC/Perception/YUFSNPCPerceptionComponent.h"
#include "NPC/Perception/YUFSPerceptionConfig.h"

namespace
{
UBoxComponent* MakeOccluder(UWorld& World, const FVector& Location)
{
	auto* Wall=World.SpawnActor<AActor>();
	auto* Box=NewObject<UBoxComponent>(Wall);
	Wall->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(5,400,200));
	Box->SetCollisionProfileName(TEXT("BlockAll"));
	Box->RegisterComponent();
	Wall->SetActorLocation(Location);
	return Box;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSLocalDoorKnowledge, "YUFS.NPC.LocalStructure.DoorSightAndRoute",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSLocalDoorKnowledge::RunTest(const FString&)
{
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
	auto* NPC=World->SpawnActor<AYUFSEvacuationNPC>();
	const double Half=NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	NPC->SetActorLocation(FVector(-100,50,Half));
	NPC->GetNPCPerceptionComponent()->Config=NewObject<UYUFSPerceptionConfig>(NPC);
	auto* Door=World->SpawnActor<AYUFSInteractionDoor>(FVector::ZeroVector,FRotator::ZeroRotator);
	auto* Upstairs=World->SpawnActor<AYUFSInteractionDoor>(FVector(0,0,365),FRotator::ZeroRotator);
	auto* Knowledge=NPC->GetExitKnowledge();
	auto* Wall=MakeOccluder(*World,FVector(-52,50,85));
	const FYUFSHazardSnapshot Empty;
	Knowledge->ObserveLocalDoors(Empty,false);
	TestEqual(TEXT("An unseen door behind a wall is not memorized"),Knowledge->GetKnownDoorCount(),0);
	Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Knowledge->ObserveLocalDoors(Empty,false);
	TestEqual(TEXT("Only the visible door on the current floor is memorized"),Knowledge->GetKnownDoorCount(),1);
	const TArray<FVector> AroundCorner={FVector(-35,55,0),FVector(155,55,0)};
	TestTrue(TEXT("A remembered door after a short path corner is recognized"),Knowledge->FindKnownDoorAlongPath(AroundCorner,0)==Door);
	const TArray<FVector> Parallel={FVector(-100,200,0),FVector(-100,300,0)};
	TestNull(TEXT("A parallel corridor route does not target the nearby door"),Knowledge->FindKnownDoorAlongPath(Parallel,0));
	FVector Crossing;
	TestFalse(TEXT("A different floor is not a door crossing"),Upstairs->IntersectPortal(FVector(-100,50,0),FVector(100,50,0),Crossing));
	TestTrue(TEXT("A reverse approach on the same floor still crosses"),Door->IntersectPortal(FVector(100,50,0),FVector(-100,50,0),Crossing));
	TestFalse(TEXT("The short portal recovery cannot walk through a closed door"),NPC->GetLocalMovement()->TryDoorwayPassage(*Door,0));
	TestFalse(TEXT("The short portal recovery cannot switch to a portal on another floor"),NPC->GetLocalMovement()->TryDoorwayPassage(*Upstairs,0));
	TestTrue(TEXT("Finding the opposite landing never moves the real door"),Door->GetPortalLandingOpposite(FVector(-100,50,0)).X>0
		&& Door->GetActorLocation().IsZero());
	Knowledge->ResetMemory();
	TestEqual(TEXT("Relocation clears remembered portals"),Knowledge->GetKnownDoorCount(),0);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSCloseSmokeGap, "YUFS.NPC.LocalStructure.CloseSmokeBetweenRays",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSCloseSmokeGap::RunTest(const FString&)
{
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
	auto* NPC=World->SpawnActor<AYUFSEvacuationNPC>();
	NPC->SetActorLocation(FVector(100,100,100));
	auto* Perception=NPC->GetNPCPerceptionComponent();
	Perception->Config=NewObject<UYUFSPerceptionConfig>(NPC);
	Perception->Config->VisionRayCount=1;
	Perception->Config->FieldOfViewDegrees=100.f;
	const auto Grid=MakeShared<FYUFSHazardGrid,ESPMode::ThreadSafe>();
	Grid->Dimensions=FIntVector(16,16,16);
	Grid->Density.Init(0,4096);
	Grid->Temperature.Init(0,4096);
	FYUFSHazardSnapshot Field;
	Field.Grid=Grid;Field.Status=EYUFSHazardDataStatus::Ready;
	Field.GridToWorld=FTransform(FQuat::Identity,FVector::ZeroVector,FVector(25));
	const FVector Cell=NPC->GetPawnViewLocation()+FRotator(-18,100.f/3.f,0).Vector()*105;
	const int32 Index=Field.CellIndex(Cell);
	if (!TestTrue(TEXT("The plume cell lies inside the fixture grid"),Grid->Density.IsValidIndex(Index)))
	{
		World->DestroyWorld(false);
		return false;
	}
	Grid->Density[Index]=255;
	auto* Wall=MakeOccluder(*World,FVector(140,100,140));
	Perception->UpdateFromSnapshot(Field,0.f);
	TestEqual(TEXT("The short fan still cannot detect smoke through a wall"),Perception->GetSmokeInFrontNormalized(),0.f);
	Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Perception->UpdateFromSnapshot(Field,1.f);
	TestTrue(TEXT("A nearby off-axis plume produces a visible smoke cue"),Perception->GetSmokeInFrontNormalized()>0.55f);
	TestEqual(TEXT("Seeing the plume does not fabricate inhaled smoke"),Perception->GetSmokeDensity(),0.f);
	World->DestroyWorld(false);
	return true;
}
#endif
