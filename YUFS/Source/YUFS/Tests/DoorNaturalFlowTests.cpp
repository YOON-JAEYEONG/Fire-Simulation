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

    TestTrue(TEXT("Auto-placement excludes a prop on either side of a closed door"),
        Door->IsInsidePropClearance(FVector(110, 0, 5), 60.f)
        && Door->IsInsidePropClearance(FVector(-110, 0, 5), 60.f));
    TestFalse(TEXT("Auto-placement can still use the other floor"),
        Door->IsInsidePropClearance(FVector(110, 0, 350), 60.f));
    TestFalse(TEXT("Auto-placement can still use space beyond the swing"),
        Door->IsInsidePropClearance(FVector(200, 0, 5), 60.f));
    TestTrue(TEXT("first nearby NPC reserves"),Door->TryReserve(User));
    TestTrue(TEXT("A walker approaching the moving leaf is predicted before contact"),
        Door->ConflictsWithOpening(*Other, FVector(-60, 180, 90), FVector(-60, 50, 90)));
    TestFalse(TEXT("A corridor beyond the swing remains available"),
        Door->ConflictsWithOpening(*Other, FVector(-200, 180, 90), FVector(-200, 30, 90)));
    TestFalse(TEXT("Door prediction does not affect another floor"),
        Door->ConflictsWithOpening(*Other, FVector(-60, 180, 440), FVector(-60, 50, 440)));
    TestFalse(TEXT("The operator is not asked to yield to their own swing"),
        Door->ConflictsWithOpening(*User, FVector(-60, 180, 90), FVector(-60, 50, 90)));
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

    auto* WideDoor=World->SpawnActor<AYUFSInteractionDoor>(FVector(3000,0,0),FRotator::ZeroRotator,Spawn);
    auto* WideDoorUser=SpawnNpc(FVector(3140,130,90));
    if (!WideDoor || !WideDoorUser) { AddError(TEXT("wide door fixture missing")); World->DestroyWorld(false); return false; }
    WideDoor->SetActorScale3D(FVector(1.f,1.8f,1.f));
    auto* TipObstacle=World->SpawnActor<AActor>();
    auto* TipBox=NewObject<UBoxComponent>(TipObstacle);
    TipObstacle->SetRootComponent(TipBox);
    TipBox->SetBoxExtent(FVector(12,12,85));
    TipBox->SetCollisionProfileName(TEXT("BlockAll"));
    TipBox->RegisterComponent();
    TipObstacle->SetActorLocation(FVector(2834,24,110));
    TestTrue(TEXT("wide leaf reserves from the handle side"),WideDoor->TryUse(WideDoorUser));
    TestTrue(TEXT("prediction reaches the tip of the rotated wide leaf"),
        WideDoor->ConflictsWithOpening(*Other,FVector(2790,-100,90),FVector(2790,100,90)));
    WideDoor->Tick(3.f);
    TestTrue(TEXT("wide leaf detects an obstruction near its open tip"),WideDoor->IsOpeningBlocked());
    TipObstacle->Destroy();
    WideDoor->Tick(3.f);
    TestTrue(TEXT("wide leaf opens after its tip is clear"),WideDoor->IsPassageClear());

    auto* InwardDoor=World->SpawnActor<AYUFSInteractionDoor>(FVector(4000,0,0),FRotator::ZeroRotator,Spawn);
    auto* InwardUser=SpawnNpc(FVector(3940,50,90));
    if (!InwardDoor || !InwardUser) { AddError(TEXT("inward door fixture missing")); World->DestroyWorld(false); return false; }
    InwardDoor->SwingSide=EYUFSDoorSwingSide::LocalNegativeX;
    TestTrue(TEXT("an operator on the swing side can reserve the door"),InwardDoor->TryReserve(InwardUser));
    TestTrue(TEXT("the operator can detect their own capsule within the inward swing"),
        InwardDoor->IsInOpeningSwing(*InwardUser,InwardUser->GetActorLocation()));
    TestFalse(TEXT("the corridor side stays clear of the inward swing"),
        InwardDoor->IsInOpeningSwing(*InwardUser,FVector(4110,65,90)));
    InwardUser->SetActorLocation(FVector(3930,140,90));
    TestTrue(TEXT("the operator can stand within reach while clear of the inward swing"),
        InwardDoor->IsUserInReach(InwardUser) && !InwardDoor->IsInOpeningSwing(*InwardUser,InwardUser->GetActorLocation()));
    TestTrue(TEXT("operator requests opening from the clear position"),InwardDoor->TryUse(InwardUser));
    InwardDoor->Tick(3.f);
    TestTrue(TEXT("fixed swing opens to local negative X from the same side"),
        InwardDoor->IsPassageClear() && InwardDoor->Panel->GetComponentLocation().X<4000.0);

    auto* Slide=World->SpawnActor<AYUFSInteractionDoor>(FVector(5000,0,0),FRotator::ZeroRotator,Spawn);
    auto* Slider=SpawnNpc(FVector(5140,80,90));
    auto* Waiting=SpawnNpc(FVector(5030,150,90));
    if (!Slide || !Slider || !Waiting) { AddError(TEXT("sliding crowd fixture missing")); World->DestroyWorld(false); return false; }
    Slide->bSlidingDoor=true;
    TestFalse(TEXT("a closed but unreserved door does not interrupt a nearby resident"),
        Slide->ShouldYieldOpeningSpace(*Waiting));
    TestTrue(TEXT("operator reserves from outside the sliding rail"),Slide->TryReserve(Slider));
    TestTrue(TEXT("courtesy signals an NPC standing in the future rail before the leaf moves"),
        Slide->ShouldYieldOpeningSpace(*Waiting));
    TestFalse(TEXT("rail courtesy does not treat the operator as a peer"),Slide->ShouldYieldOpeningSpace(*Slider));
    TestTrue(TEXT("the close approach pocket is reserved for the operator"),
        Slide->IsInOperatorApproach(*Waiting,FVector(5100,80,90)));
    TestFalse(TEXT("a queue pocket on the next floor is not affected"),
        Slide->IsInOperatorApproach(*Waiting,FVector(5100,80,440)));
    TestTrue(TEXT("operator requests the sliding motion"),Slide->TryUse(Slider));
    Slide->Tick(1.f);
    TestTrue(TEXT("a physical peer still blocks the leaf"),Slide->IsOpeningBlocked());
    TestFalse(TEXT("no passage while a body occupies the rail"),Slide->IsPassageClear());
    Waiting->SetActorLocation(FVector(5220,150,90));
    TestFalse(TEXT("a peer who steps back is no longer in the rail"),Slide->ShouldYieldOpeningSpace(*Waiting));
    Slide->Tick(2.f);
    TestTrue(TEXT("the same reservation completes as soon as the rail is free"),Slide->IsPassageClear());
    TestFalse(TEXT("an already open door does not continue queue control"),
        Slide->IsInOperatorApproach(*Waiting,FVector(5100,80,90)));

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
