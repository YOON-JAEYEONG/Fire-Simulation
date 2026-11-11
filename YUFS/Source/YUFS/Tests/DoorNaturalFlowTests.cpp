#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Fire/YUFSInteractionDoor.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "Simulation/YUFSSimulationController.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSDoorNaturalFlowTest,
    "YUFS.NPC.Interaction.DoorReservationAndCollision",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FYUFSDoorNaturalFlowTest::RunTest(const FString& Parameters)
{
    const auto Settings=UWorld::InitializationValues().AllowAudioPlayback(false)
        .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Settings);
    if (!TestNotNull(TEXT("isolated door physics world"),World)) return false;
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto SpawnNpc=[&](FVector Location)
    {
        return World->SpawnActor<AYUFSEvacuationNPC>(Location,FRotator::ZeroRotator,Spawn);
    };
    auto* Door=World->SpawnActor<AYUFSInteractionDoor>(FVector::ZeroVector,FRotator::ZeroRotator,Spawn);
    auto* User=SpawnNpc(FVector(140,50,90));
    auto* Other=SpawnNpc(FVector(-140,50,90));
    if (!Door || !User || !Other) { AddError(TEXT("test actors missing")); World->DestroyWorld(false); return false; }
    const FTransform DoorTransform=Door->GetActorTransform();
    const FVector UserLocation=User->GetActorLocation();
    const FQuat ClosedRotation=Door->Panel->GetComponentQuat();

    TestTrue(TEXT("first nearby NPC reserves"),Door->TryReserve(User));
    TestFalse(TEXT("competing NPC cannot steal reservation"),Door->TryReserve(Other));
    Door->Tick(.5f);
    TestTrue(TEXT("reservation alone does not animate the leaf"),Door->Panel->GetComponentQuat().Equals(ClosedRotation));
    TestFalse(TEXT("reservation does not permit passage"),Door->IsPassageClear());
    TestTrue(TEXT("reserved NPC explicitly begins opening"),Door->TryUse(User));
    Door->Tick(.4f);
    TestFalse(TEXT("partial opening is not passage completion"),Door->IsPassageClear());
    const FQuat PartialRotation=Door->Panel->GetComponentQuat();
    TestFalse(TEXT("leaf actually rotated"),PartialRotation.Equals(ClosedRotation));
    Door->Release(User);
    Door->Tick(3.f);
    TestTrue(TEXT("cancellation stops the leaf without resetting it"),Door->Panel->GetComponentQuat().Equals(PartialRotation));
    TestFalse(TEXT("cancelled owner releases reservation"),Door->IsReservedBy(User));
    TestTrue(TEXT("opposite-side NPC can resume the same leaf"),Door->TryUse(Other));
    Door->Tick(3.f);
    TestTrue(TEXT("full opening releases the doorway guard"),Door->IsPassageClear());
    TestTrue(TEXT("open solid leaf keeps physical collision"),Door->Panel->GetCollisionEnabled()==ECollisionEnabled::QueryAndPhysics);
    TestTrue(TEXT("door remains the same authored actor transform"),Door->GetActorTransform().Equals(DoorTransform));
    TestTrue(TEXT("opening never teleports the user"),User->GetActorLocation().Equals(UserLocation));
    TestFalse(TEXT("completed door releases operator"),Door->IsReservedBy(Other));

    auto* BlockedDoor=World->SpawnActor<AYUFSInteractionDoor>(FVector(1000,0,0),FRotator::ZeroRotator,Spawn);
    auto* BlockedUser=SpawnNpc(FVector(1140,50,90));
    if (!BlockedDoor || !BlockedUser) { AddError(TEXT("blocked fixture missing")); World->DestroyWorld(false); return false; }
    BlockedDoor->bLocked=true;
    TestFalse(TEXT("locked door cannot be reserved"),BlockedDoor->TryUse(BlockedUser));
    BlockedDoor->bLocked=false; BlockedDoor->bHot=true;
    TestFalse(TEXT("hot door cannot be reserved"),BlockedDoor->TryUse(BlockedUser));
    BlockedDoor->bHot=false;
    TestFalse(TEXT("far-away NPC cannot reserve"),BlockedDoor->TryReserve(User));

    auto* Obstacle=World->SpawnActor<AActor>();
    auto* Box=NewObject<UBoxComponent>(Obstacle);
    Obstacle->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(15,15,85));
    Box->SetCollisionProfileName(TEXT("BlockAll"));
    Box->RegisterComponent();
    Obstacle->SetActorLocation(FVector(930,65,110));
    TestTrue(TEXT("clear handle can be reserved while arc is obstructed"),BlockedDoor->TryUse(BlockedUser));
    BlockedDoor->Tick(3.f);
    TestTrue(TEXT("rotation detects a solid object in the entire swing arc"),BlockedDoor->IsOpeningBlocked());
    TestFalse(TEXT("obstructed opening never releases passage"),BlockedDoor->IsPassageClear());
    Obstacle->Destroy();
    BlockedDoor->Tick(3.f);
    TestTrue(TEXT("removing obstruction allows the reserved action to finish"),BlockedDoor->IsPassageClear());

    auto* PausedDoor=World->SpawnActor<AYUFSInteractionDoor>(FVector(2000,0,0),FRotator::ZeroRotator,Spawn);
    auto* PausedUser=SpawnNpc(FVector(2140,50,90));
    auto* Controller=World->SpawnActor<AYUFSSimulationController>();
    if (!PausedDoor || !PausedUser || !Controller) { AddError(TEXT("pause fixture missing")); World->DestroyWorld(false); return false; }
    Controller->bPreviewAllNPCActionAnimations=false;
    Controller->bEnableTimelineRecording=false;
    Controller->FireStartDelaySeconds=0.f;
    TestTrue(TEXT("door can reserve before simulation starts"),PausedDoor->TryUse(PausedUser));
    PausedDoor->Tick(3.f);
    TestFalse(TEXT("waiting-for-Start cannot advance a door action"),PausedDoor->IsOpen());
    Controller->StartSimulation();
    Controller->Tick(.1f);
    Controller->PauseSimulation();
    PausedDoor->Tick(3.f);
    TestFalse(TEXT("Pause preserves door opening progress"),PausedDoor->IsOpen());
    Controller->ResumeSimulation();
    PausedDoor->Tick(3.f);
    TestTrue(TEXT("Resume permits opening again"),PausedDoor->IsPassageClear());

    World->DestroyWorld(false);
    return true;
}

#endif
