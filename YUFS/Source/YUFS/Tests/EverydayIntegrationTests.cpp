#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AIController.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Animation/YUFSActionAnimationComponent.h"
#include "NPC/Behavior/YUFSBehaviorConfig.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "NPC/Cognition/YUFSHumanCognitionComponent.h"
#include "Simulation/YUFSSimulationController.h"

struct FYUFSEverydayIntegrationTestAccess
{
    static void Initialize(AYUFSEvacuationNPC* Npc, AYUFSSimulationController* Controller)
    {
        Npc->SimulationController = Controller;
        Npc->bLogTransitions = Npc->bLogDecisionTrace = false;
        Npc->bDataCollectionMode = true;
        Npc->bUseExternalMotionDriver = true;
        Npc->StableNPCId = 37;
        Npc->DeterministicRng.Initialize(173, 37);
        Npc->MLPolicy.SetFallbackRandomSource(&Npc->DeterministicRng);
        Npc->HumanCognitionComp->Initialize(37, Npc->DeterministicRng);
        Npc->EverydayActivityTimer = 100.f;
        Npc->BehaviorSM->Config = NewObject<UYUFSBehaviorConfig>(Npc);
        Npc->BehaviorSM->InitializePersonality(37);
    }
    static bool IsEverydayActive(const AYUFSEvacuationNPC* Npc) { return Npc->bEverydayBehaviorActive; }
    static void SetController(AYUFSEvacuationNPC* Npc, AYUFSSimulationController* Controller)
    { Npc->SimulationController = Controller; }
    static void UseExternalNavigation(AYUFSEvacuationNPC* Npc) { Npc->bUseExternalNavigationDriver = true; }
    static void Animate(AYUFSEvacuationNPC* Npc) { Npc->UpdateActionAnimation(); }
};

namespace
{
struct FEverydayFixture
{
    UWorld* World = nullptr;
    AYUFSEvacuationNPC* Npc = nullptr;
    AYUFSSimulationController* Controller = nullptr;
    FEverydayFixture()
    {
        const auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false)
            .CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
        World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
            ERHIFeatureLevel::Num, &Settings);
        FActorSpawnParameters Spawn;
        Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Controller = World->SpawnActor<AYUFSSimulationController>();
        Controller->bEnableTimelineRecording = false;
        Npc = World->SpawnActor<AYUFSEvacuationNPC>(FVector(0, 0, 90), FRotator::ZeroRotator, Spawn);
        World->SpawnActor<AAIController>()->Possess(Npc);
        FYUFSEverydayIntegrationTestAccess::Initialize(Npc, Controller);
    }
    ~FEverydayFixture() { World->DestroyWorld(false); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEverydayPauseReplayTest,
    "YUFS.NPC.Integration.Everyday.PauseResumeAndReplay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEverydayPauseReplayTest::RunTest(const FString&)
{
    FEverydayFixture F;
    F.Npc->Tick(.1f);
    TestTrue(TEXT("Waiting retains CYW everyday activity"), FYUFSEverydayIntegrationTestAccess::IsEverydayActive(F.Npc));
    TestEqual(TEXT("Everyday walking speed is preserved"), F.Npc->GetCharacterMovement()->MaxWalkSpeed, F.Npc->EverydayWalkSpeedCmPerSecond);
    TestEqual(TEXT("Everyday movement is not an evacuation policy action"), F.Npc->GetLastAction(), EYUFSAction::Idle);
    F.Controller->StartSimulation();
    F.Controller->PauseSimulation();
    F.Npc->Tick(.1f);
    TestFalse(TEXT("Pause stops everyday movement"), FYUFSEverydayIntegrationTestAccess::IsEverydayActive(F.Npc));
    F.Controller->ResumeSimulation();
    F.Npc->Tick(.1f);
    TestTrue(TEXT("Resume during fire delay restores everyday activity"), FYUFSEverydayIntegrationTestAccess::IsEverydayActive(F.Npc));
    F.Npc->SetTimelinePlaybackMode(true);
    F.Npc->Tick(.1f);
    TestFalse(TEXT("Replay cannot be driven by everyday movement"), FYUFSEverydayIntegrationTestAccess::IsEverydayActive(F.Npc));
    TestEqual(TEXT("Replay disables live movement"), F.Npc->GetCharacterMovement()->MovementMode.GetValue(), MOVE_None);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEverydayAlarmHandoffTest,
    "YUFS.NPC.Integration.Everyday.AlarmHandsOffToJJW",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEverydayAlarmHandoffTest::RunTest(const FString&)
{
    FEverydayFixture F;
    F.Npc->Tick(.1f);
    FYUFSNPCObservation Observation;
    Observation.bAlarmSounding = true;
    F.Npc->GetBehaviorStateMachine()->TickStateMachine(.1f, Observation);
    TestEqual(TEXT("Real alarm observation enters perceiving"), F.Npc->GetBehaviorStateMachine()->GetCurrentState(), EYUFSBehaviorState::Perceiving);
    FYUFSEverydayIntegrationTestAccess::SetController(F.Npc, nullptr);
    F.Npc->Tick(.1f);
    TestFalse(TEXT("Perception cancels the everyday controller"), FYUFSEverydayIntegrationTestAccess::IsEverydayActive(F.Npc));
    TestEqual(TEXT("JJW policy resumes information seeking"), F.Npc->GetLastAction(), EYUFSAction::SeekInformation);
    TestFalse(TEXT("Alarm alone cannot authorize suppression"), F.Npc->AllowsOptionalInteractions());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEverydayExternalDriverTest,
    "YUFS.NPC.Integration.Everyday.RespectsExternalNavigation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEverydayExternalDriverTest::RunTest(const FString&)
{
    FEverydayFixture F;
    FYUFSEverydayIntegrationTestAccess::UseExternalNavigation(F.Npc);
    F.Npc->Tick(.1f);
    TestFalse(TEXT("Native everyday movement does not compete with the external driver"), FYUFSEverydayIntegrationTestAccess::IsEverydayActive(F.Npc));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEverydayNativeAnimationTest,
    "YUFS.NPC.Integration.Everyday.WalkingAnimationKeepsIdlePolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEverydayNativeAnimationTest::RunTest(const FString&)
{
    FEverydayFixture F;
    auto* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/NPCs/Crawling__1_.Crawling__1_"));
    if (!TestNotNull(TEXT("Existing RL avatar loads"), Mesh)) return false;
    F.Npc->GetMesh()->SetSkeletalMesh(Mesh);
    F.Npc->bUseExternalMotionDriver = false;
    F.Npc->GetActionAnimationComponent()->Initialize(F.Npc->GetMesh(), 37);
    F.Npc->Tick(.1f);
    F.Npc->GetCharacterMovement()->Velocity = FVector(140, 0, 0);
    FYUFSEverydayIntegrationTestAccess::Animate(F.Npc);
    TestEqual(TEXT("Everyday motion selects the compatible walking sequence"), F.Npc->GetCurrentActionAnimationName(), FString(TEXT("Walking")));
    TestEqual(TEXT("Walking presentation does not fabricate a helping policy action"), F.Npc->GetLastAction(), EYUFSAction::Idle);
    F.Npc->GetCharacterMovement()->Velocity = FVector::ZeroVector;
    FYUFSEverydayIntegrationTestAccess::Animate(F.Npc);
    TestEqual(TEXT("Everyday stop restores idle animation"), F.Npc->GetCurrentActionAnimationName(), FString(TEXT("Idle")));
    return true;
}

#endif
