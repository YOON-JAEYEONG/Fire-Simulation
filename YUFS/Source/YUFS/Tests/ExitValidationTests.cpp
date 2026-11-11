#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Level/YUFSLevelDataManager.h"
#include "Level/YUFSExitPoint.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "Core/YUFSObservation.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "NPC/Navigation/YUFSLocalMovementComponent.h"
#include "NPC/Navigation/YUFSExitKnowledgeComponent.h"
#include "Fire/YUFSInteractionDoor.h"
#include "Simulation/YUFSSimulationController.h"
#include "GameFramework/CharacterMovementComponent.h"

struct FYUFSExitValidationTestAccess
{
    static void SetManager(AYUFSEvacuationNPC* NPC, AYUFSLevelDataManager* Manager) { NPC->LevelDataMgr = Manager; }
    static EYUFSTerminalReason Terminal(AYUFSEvacuationNPC* NPC) { return NPC->GetCurrentTerminalReason(); }
    static FVector Choose(AYUFSEvacuationNPC* NPC) { return NPC->ChooseKnownExit(); }
    static void AvoidUntil(AYUFSEvacuationNPC* NPC, FVector Exit, float Until) { NPC->FailedExitUntil.Add(Exit, Until); }
    static void SetController(AYUFSEvacuationNPC* NPC,AYUFSSimulationController* Controller) { NPC->SimulationController=Controller; }
    static void SyncPosture(AYUFSEvacuationNPC* NPC) { NPC->UpdatePhysicalPosture(); }
};

namespace
{
struct FExitGeometryFixture
{
    UWorld* World;
    AYUFSLevelDataManager* Manager;
    AYUFSEvacuationNPC* NPC;
    AActor* Floor;
    FExitGeometryFixture()
    {
        const auto Settings=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
        World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Settings);
        Floor=Box(FVector(0,0,-10),FVector(600,600,10));
        World->SpawnActor<AYUFSExitPoint>();
        Manager=World->SpawnActor<AYUFSLevelDataManager>(); Manager->DispatchBeginPlay();
        NPC=World->SpawnActor<AYUFSEvacuationNPC>();
        NPC->SetActorLocation(FVector(-80,0,NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.f));
        FYUFSExitValidationTestAccess::SetManager(NPC,Manager);
        auto* Mind=NPC->GetBehaviorStateMachine(); Mind->Config=NewObject<UYUFSBehaviorConfig>(Mind);
        Mind->InitializePersonality(9); FYUFSNPCObservation Cue; Cue.NearbyHeat=.9f; Mind->TickStateMachine(.1f,Cue);
    }
    AActor* Box(FVector Center,FVector Extent)
    {
        auto* Actor=World->SpawnActor<AActor>(); auto* Shape=NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Shape); Shape->SetBoxExtent(Extent); Shape->SetCollisionProfileName(TEXT("BlockAll"));
        Shape->RegisterComponent(); Actor->SetActorLocation(Center); return Actor;
    }
    ~FExitGeometryFixture() { World->DestroyWorld(false); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSExitPhysicalGeometryTest,
    "YUFS.NPC.Navigation.ExitValidation.WallsDoorsAndCrowdGeometry",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSExitPhysicalGeometryTest::RunTest(const FString&)
{
    FExitGeometryFixture F; const FVector Initial=F.NPC->GetActorLocation();
    TestEqual(TEXT("A clear supported origin exit remains reachable"),FYUFSExitValidationTestAccess::Terminal(F.NPC),EYUFSTerminalReason::ReachedExit);
    auto* Wall=F.Box(FVector(-40,0,150),FVector(5,180,150));
    TestEqual(TEXT("A wall inside the success radius cannot count as evacuation"),FYUFSExitValidationTestAccess::Terminal(F.NPC),EYUFSTerminalReason::None);
    Wall->Destroy();
    auto* Door=F.World->SpawnActor<AYUFSInteractionDoor>(FVector(-40,-50,0),FRotator::ZeroRotator);
    TestEqual(TEXT("A closed exit door does not count as success through its leaf"),FYUFSExitValidationTestAccess::Terminal(F.NPC),EYUFSTerminalReason::None);
    TestTrue(TEXT("Door fixture can be opened"),Door->TryUse(F.NPC)); Door->Tick(.2f);
    TestEqual(TEXT("A partially opened guarded doorway is not success"),FYUFSExitValidationTestAccess::Terminal(F.NPC),EYUFSTerminalReason::None);
    Door->Tick(3.f);
    TestTrue(TEXT("Door opening completed"),Door->IsPassageClear());
    TestEqual(TEXT("An actually clear open door permits success"),FYUFSExitValidationTestAccess::Terminal(F.NPC),EYUFSTerminalReason::ReachedExit);
    Door->Destroy();
    auto* Peer=F.World->SpawnActor<AYUFSEvacuationNPC>(); Peer->SetActorLocation(FVector(0,0,90));
    TestFalse(TEXT("A nearby body still blocks actual recovery movement"),F.NPC->GetLocalMovement()->IsPhysicalCorridorClear(FVector::ZeroVector));
    TestEqual(TEXT("Another resident does not turn a valid exit region into a wall"),FYUFSExitValidationTestAccess::Terminal(F.NPC),EYUFSTerminalReason::ReachedExit);
    TestTrue(TEXT("Success queries never move the NPC"),F.NPC->GetActorLocation().Equals(Initial));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSExitFloorGeometryTest,
    "YUFS.NPC.Navigation.ExitValidation.FloorSeparationAndMissingSupport",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSExitFloorGeometryTest::RunTest(const FString&)
{
    FExitGeometryFixture F;
    auto* Upper=F.Box(FVector(0,0,45),FVector(180,180,5));
    F.NPC->SetActorLocation(FVector(0,0,140)); // feet52, slab top50; centre is still inside old 150cm radius
    TestEqual(TEXT("A different supporting slab is not the exit floor"),FYUFSExitValidationTestAccess::Terminal(F.NPC),EYUFSTerminalReason::None);
    Upper->Destroy(); Upper=F.Box(FVector(0,0,35),FVector(180,180,5)); F.NPC->SetActorLocation(FVector(0,0,130));
    TestEqual(TEXT("A nearby slab within one step still must not replace the exit's actual floor"),FYUFSExitValidationTestAccess::Terminal(F.NPC),EYUFSTerminalReason::None);
    Upper->Destroy(); F.NPC->SetActorLocation(FVector(0,0,90));
    TestEqual(TEXT("The actual supported exit floor counts"),FYUFSExitValidationTestAccess::Terminal(F.NPC),EYUFSTerminalReason::ReachedExit);
    F.Floor->Destroy();
    TestEqual(TEXT("An unsupported floating exit coordinate is not evacuation"),FYUFSExitValidationTestAccess::Terminal(F.NPC),EYUFSTerminalReason::None);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSExitConfiguredRadiusTest,
    "YUFS.NPC.Navigation.ExitValidation.ConfiguredRadiusSharedWithNPC",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSExitConfiguredRadiusTest::RunTest(const FString&)
{
    FExitGeometryFixture F; auto* Controller=F.World->SpawnActor<AYUFSSimulationController>();
    FYUFSExitValidationTestAccess::SetController(F.NPC,Controller); F.NPC->SetActorLocation(FVector(200,0,90));
    Controller->EvacuationSuccessDistanceCm=300.f;
    TestEqual(TEXT("NPC terminal uses the controller's configured success radius"),FYUFSExitValidationTestAccess::Terminal(F.NPC),EYUFSTerminalReason::ReachedExit);
    Controller->EvacuationSuccessDistanceCm=100.f;
    TestEqual(TEXT("A smaller configured radius is respected too"),FYUFSExitValidationTestAccess::Terminal(F.NPC),EYUFSTerminalReason::None);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSExitCrawlGeometryTest,
    "YUFS.NPC.Navigation.ExitValidation.ActualCrawlCapsuleForLowClearance",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSExitCrawlGeometryTest::RunTest(const FString&)
{
    FExitGeometryFixture F;
    F.Box(FVector(-40,0,130),FVector(5,180,30)); // 100cm headroom at the approach
    TestEqual(TEXT("A standing body cannot count through a low obstacle"),FYUFSExitValidationTestAccess::Terminal(F.NPC),EYUFSTerminalReason::None);
    F.NPC->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    FYUFSNPCObservation Smoke; Smoke.SmokeDensityAtSelf=1.f;
    F.NPC->GetBehaviorStateMachine()->TickStateMachine(26.f,Smoke);
    FYUFSExitValidationTestAccess::SyncPosture(F.NPC);
    TestTrue(TEXT("Fixture uses the actual low collision shape"),F.NPC->IsCrouched());
    TestEqual(TEXT("A lowered body can use the supported clear approach"),FYUFSExitValidationTestAccess::Terminal(F.NPC),EYUFSTerminalReason::ReachedExit);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSExitFailureMemoryTest,
    "YUFS.NPC.Navigation.ExitValidation.ChooseAlternativeOrWait",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSExitFailureMemoryTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    auto* Manager = World->SpawnActor<AYUFSLevelDataManager>();
    auto* NPC = World->SpawnActor<AYUFSEvacuationNPC>();
    NPC->SetActorLocation(FVector(100, 0, 0));
    auto* A = World->SpawnActor<AYUFSExitPoint>(); A->SetActorLocation(FVector(300, 0, 0));
    auto* B = World->SpawnActor<AYUFSExitPoint>(); B->SetActorLocation(FVector(1000, 0, 0));
    A->ExitID=TEXT("KnownA");B->ExitID=TEXT("KnownB");
    NPC->GetExitKnowledge()->InitiallyKnownExitIDs={A->ExitID,B->ExitID};
    Manager->DispatchBeginPlay();
    FYUFSExitValidationTestAccess::SetManager(NPC, Manager);
    TestEqual(TEXT("Initially chooses the nearer exit"), FYUFSExitValidationTestAccess::Choose(NPC), A->GetActorLocation());
    FYUFSExitValidationTestAccess::AvoidUntil(NPC, A->GetActorLocation(), 10.f);
    TestEqual(TEXT("Recent failure selects another exit instead of reselecting the blocked one"), FYUFSExitValidationTestAccess::Choose(NPC), B->GetActorLocation());
    FYUFSExitValidationTestAccess::AvoidUntil(NPC, B->GetActorLocation(), 10.f);
    TestTrue(TEXT("All recently failed exits produce a wait rather than an arbitrary forced route"), FYUFSExitValidationTestAccess::Choose(NPC).IsZero());
    FYUFSExitValidationTestAccess::AvoidUntil(NPC, A->GetActorLocation(), -1.f);
    TestEqual(TEXT("Expired cooldown permits reassessment"), FYUFSExitValidationTestAccess::Choose(NPC), A->GetActorLocation());
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSExitValidationTest,
    "YUFS.NPC.Navigation.ExitValidation.MissingDestroyedAndOrigin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSExitValidationTest::RunTest(const FString&)
{
    const auto Settings=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Settings);
    auto* Floor=World->SpawnActor<AActor>(); auto* Shape=NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(Shape); Shape->SetBoxExtent(FVector(200,200,10)); Shape->SetCollisionProfileName(TEXT("BlockAll"));
    Shape->RegisterComponent(); Floor->SetActorLocation(FVector(0,0,-10));
    auto* Manager = World->SpawnActor<AYUFSLevelDataManager>();
    auto* NPC = World->SpawnActor<AYUFSEvacuationNPC>();
    NPC->SetActorLocation(FVector(0,0,NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.f));
    FYUFSExitValidationTestAccess::SetManager(NPC, Manager);
    auto* Mind = NPC->GetBehaviorStateMachine();
    Mind->Config = NewObject<UYUFSBehaviorConfig>(Mind);
    Mind->InitializePersonality(9);
    FYUFSNPCObservation Observation;
    Observation.NearbyHeat = .9f;
    Mind->TickStateMachine(.1f, Observation);
    TestEqual(TEXT("Fixture is evacuating"), Mind->GetCurrentState(), EYUFSBehaviorState::Evacuating);
    TestEqual(TEXT("No exit cannot count as evacuation"), FYUFSExitValidationTestAccess::Terminal(NPC), EYUFSTerminalReason::None);
    auto* Exit = World->SpawnActor<AYUFSExitPoint>();
    Manager->DispatchBeginPlay();
    TestEqual(TEXT("A real exit at world origin remains valid"), FYUFSExitValidationTestAccess::Terminal(NPC), EYUFSTerminalReason::ReachedExit);
    Exit->Destroy();
    TestEqual(TEXT("Destroyed exit cannot count as evacuation"), FYUFSExitValidationTestAccess::Terminal(NPC), EYUFSTerminalReason::None);
    World->DestroyWorld(false);
    return true;
}
#endif
