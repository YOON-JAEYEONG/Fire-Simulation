#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "Fire/YUFSInteractionDoor.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "Simulation/YUFSTimelineTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSSlidingDoorTest,
    "YUFS.NPC.Interaction.SlidingDoorTrackAndPassage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSSlidingDoorTest::RunTest(const FString&)
{
    const auto Settings=UWorld::InitializationValues().AllowAudioPlayback(false)
        .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Settings);
    if (!TestNotNull(TEXT("sliding-door world"),World)) return false;
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Door=World->SpawnActor<AYUFSInteractionDoor>(FVector::ZeroVector,FRotator::ZeroRotator,Spawn);
    auto* Operator=World->SpawnActor<AYUFSEvacuationNPC>(FVector(-105,65,90),FRotator::ZeroRotator,Spawn);
    auto* Obstruction=World->SpawnActor<AYUFSEvacuationNPC>(FVector(30,-90,90),FRotator::ZeroRotator,Spawn);
    if (!Door || !Operator || !Obstruction) { World->DestroyWorld(false); return false; }
    TestFalse(TEXT("An untagged legacy door stays hinged"), Door->UsesSlidingMotion());
    Door->Tags.Add(TEXT("MergeFinalDoor"));
    Door->DispatchBeginPlay();
    TestTrue(TEXT("The authored building tag opts existing doors into sliding"),Door->UsesSlidingMotion());
    const FVector Closed=Door->Panel->GetComponentLocation();
    const FQuat Rotation=Door->Panel->GetComponentQuat();
    Door->bLocked=true;
    TestFalse(TEXT("Sliding does not override the lock"),Door->TryUse(Operator));
    Door->bLocked=false;
    TestTrue(TEXT("A person on the clear side requests a slide"), Door->TryUse(Operator));
    Door->Tick(.25f);
    const FVector AtPartial=Door->Panel->GetComponentLocation();
    TestTrue(TEXT("The leaf translates without rotating or moving toward the corridor"),
        !AtPartial.Equals(Closed) && Door->Panel->GetComponentQuat().Equals(Rotation)
        && FMath::IsNearlyEqual(AtPartial.X,Closed.X));
    TestFalse(TEXT("The doorway is still closed while the leaf is partial"),Door->IsPassageClear());
    Door->Tick(2.f);
    TestTrue(TEXT("A person standing along the slide rail blocks the sweep"),Door->IsOpeningBlocked());
    TestFalse(TEXT("The blocker never causes a false passage-clear"),Door->IsPassageClear());
    Obstruction->SetActorLocation(FVector(500,-300,90));
    Door->Tick(2.f);
    TestTrue(TEXT("Clearing the real rail lets the doorway open"),Door->IsPassageClear());
    const FVector Open=Door->Panel->GetComponentLocation();
    TestTrue(TEXT("Fully open panel clears the aperture and remains visible and solid"),
        FMath::Abs(Open.Y-Closed.Y)>100.0 && Door->Panel->IsVisible()
        && Door->Panel->GetCollisionEnabled()==ECollisionEnabled::QueryAndPhysics);
    FYUFSTimelineDoorSnapshot Snapshot=Door->BuildTimelineSnapshot();
    Snapshot.OpenFraction=.5f;
    Door->ApplyTimelineSnapshot(Snapshot);
    TestTrue(TEXT("Review replays sliding pose, not a swing"),
        Door->Panel->GetComponentQuat().Equals(Rotation)
        && FMath::IsNearlyEqual(Door->Panel->GetComponentLocation().Y,(Closed.Y+Open.Y)*.5));
    TestFalse(TEXT("Reviewing a partial frame restores the doorway blocker"),Door->IsPassageClear());
    World->DestroyWorld(false);
    return true;
}
#endif
