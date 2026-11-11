#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "AIController.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
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
        if (Controller) Controller->RegisterNPC(Npc);
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
    static void SetWalk(AYUFSEvacuationNPC* Npc)
    {
        Npc->EverydayPath = {Npc->GetNavAgentLocation(), Npc->GetNavAgentLocation() + FVector(600, 0, 0)};
        Npc->EverydayWaypoint = 1;
        Npc->EverydayDestination = Npc->EverydayPath.Last();
        Npc->EverydayActivityTimer = 10.f;
        Npc->bEverydayRoaming = true;
    }
    static float Timer(const AYUFSEvacuationNPC* Npc) { return Npc->EverydayActivityTimer; }
    static int32 RandomState(const AYUFSEvacuationNPC* Npc) { return Npc->EverydayRandomStream.GetCurrentSeed(); }
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
    FEverydayFixture(bool bPhysics = false)
    {
        const auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false)
            .CreatePhysicsScene(bPhysics).CreateNavigation(false).CreateAISystem(false);
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
    F.Controller->Tick(.1f);
    F.Npc->Tick(.1f);
	TestFalse(TEXT("Waiting keeps everyday activity stopped before Start"), FYUFSEverydayIntegrationTestAccess::IsEverydayActive(F.Npc));
	TestEqual(TEXT("Waiting keeps NPC movement speed at zero"), F.Npc->GetCharacterMovement()->MaxWalkSpeed, 0.f);
	TestEqual(TEXT("Waiting disables physical movement and collision depenetration before Start"),
		F.Npc->GetCharacterMovement()->MovementMode.GetValue(), MOVE_None);
	F.Controller->StartSimulation();
	F.Controller->Tick(.1f);
    F.Npc->Tick(.1f);
	TestTrue(TEXT("Start enables CYW everyday activity"), FYUFSEverydayIntegrationTestAccess::IsEverydayActive(F.Npc));
    TestEqual(TEXT("Everyday walking speed is preserved"), F.Npc->GetCharacterMovement()->MaxWalkSpeed, F.Npc->EverydayWalkSpeedCmPerSecond);
    TestEqual(TEXT("Everyday movement is not an evacuation policy action"), F.Npc->GetLastAction(), EYUFSAction::Idle);
    F.Controller->PauseSimulation();
    F.Controller->Tick(.1f);
    TestFalse(TEXT("Pause stops physical movement"), F.Npc->GetCharacterMovement()->IsComponentTickEnabled());
    F.Controller->ResumeSimulation();
    F.Controller->Tick(.1f);
    F.Npc->Tick(.1f);
    TestTrue(TEXT("Resume during fire delay restores everyday activity"), FYUFSEverydayIntegrationTestAccess::IsEverydayActive(F.Npc));
    F.Npc->SetTimelinePlaybackMode(true);
    F.Controller->Tick(.1f);
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
    F.Controller->StartSimulation();
    F.Controller->Tick(.1f);
    F.Npc->Tick(.1f);
    FYUFSNPCObservation Observation;
    Observation.bAlarmSounding = true;
    F.Npc->GetBehaviorStateMachine()->TickStateMachine(.1f, Observation);
    TestEqual(TEXT("Real alarm observation enters perceiving"), F.Npc->GetBehaviorStateMachine()->GetCurrentState(), EYUFSBehaviorState::Perceiving);
    FYUFSEverydayIntegrationTestAccess::SetController(F.Npc, nullptr);
    F.Controller->Tick(.1f);
    F.Npc->Tick(.1f);
    TestFalse(TEXT("Perception cancels the everyday controller"), FYUFSEverydayIntegrationTestAccess::IsEverydayActive(F.Npc));
    TestTrue(TEXT("Emergency handoff restores automatic movement"), F.Npc->GetCharacterMovement()->IsComponentTickEnabled());
    TestTrue(TEXT("Emergency handoff restores crowd avoidance"), F.Npc->GetCharacterMovement()->bUseRVOAvoidance);
    TestEqual(TEXT("JJW policy resumes information seeking"), F.Npc->GetLastAction(), EYUFSAction::SeekInformation);
    TestFalse(TEXT("Alarm alone cannot authorize suppression"), F.Npc->AllowsOptionalInteractions());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNoEverydayStartMovementTest,
    "YUFS.NPC.Integration.Everyday.StartRestoresMovementWithoutRoaming",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNoEverydayStartMovementTest::RunTest(const FString&)
{
    for (const float Delay : {0.f, 2.f})
    {
        FEverydayFixture F;
        F.Controller->RegisterNPC(F.Npc);
        F.Npc->bEnableEverydayBehavior = false;
        F.Controller->FireStartDelaySeconds = Delay;
        auto* Movement = F.Npc->GetCharacterMovement();
        Movement->SetMovementMode(MOVE_Falling);
        F.Npc->Tick(.1f);
        F.Npc->Tick(.1f);
        TestEqual(TEXT("Repeated pre-start ticks keep physical movement held"), Movement->MovementMode.GetValue(), MOVE_None);
        F.Controller->StartSimulation();
        F.Controller->Tick(.1f);
        F.Npc->Tick(.1f);
        TestEqual(TEXT("Start restores the original mode without everyday roaming"), Movement->MovementMode.GetValue(), MOVE_Falling);
        TestFalse(TEXT("Movement restoration does not enable roaming"), FYUFSEverydayIntegrationTestAccess::IsEverydayActive(F.Npc));
        F.Controller->PauseSimulation();
        TestFalse(TEXT("Pause still stops the component clock"), Movement->IsComponentTickEnabled());
        F.Controller->ResumeSimulation();
        TestEqual(TEXT("Pause/resume retains the restored mode"), Movement->MovementMode.GetValue(), MOVE_Falling);
        F.Npc->SetTimelinePlaybackMode(true);
        F.Npc->Tick(.1f);
        TestEqual(TEXT("Historical review never releases a live movement hold"), Movement->MovementMode.GetValue(), MOVE_None);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEverydayExternalDriverTest,
    "YUFS.NPC.Integration.Everyday.RespectsExternalNavigation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEverydayExternalDriverTest::RunTest(const FString&)
{
    FEverydayFixture F;
    FYUFSEverydayIntegrationTestAccess::UseExternalNavigation(F.Npc);
	F.Controller->StartSimulation();
    F.Controller->Tick(.1f);
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
	F.Controller->StartSimulation();
    F.Controller->Tick(.1f);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEverydayFixedStepRegression,
    "YUFS.NPC.Integration.Everyday.FixedStepAcrossFrameRates",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEverydayFixedStepRegression::RunTest(const FString&)
{
    auto Run = [&](int32 Frames, float Delta, bool bWalk, FVector& Position, float& Timer, int32& Random)
    {
        FEverydayFixture F(true);
        auto* Floor = F.World->SpawnActor<AActor>();
        auto* Box = NewObject<UBoxComponent>(Floor);
        Floor->SetRootComponent(Box);
        Box->SetBoxExtent(FVector(2000,2000,10));
        Box->SetCollisionProfileName(TEXT("BlockAll"));
        Box->RegisterComponent();
        Floor->SetActorLocation(FVector(0,0,-10));
        F.Npc->SetActorLocation(FVector(0,0,F.Npc->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2));
        F.Controller->Tick(float(Frames)); // Time spent waiting before Start cannot consume the walk.
        F.Npc->EverydayRoamChance = 0.f; // Exercise repeated seeded idle draws without a NavMesh fixture.
        F.Controller->StartSimulation();
        if (bWalk) FYUFSEverydayIntegrationTestAccess::SetWalk(F.Npc);
        for (int32 I=0; I<Frames; ++I) F.Controller->Tick(Delta);
        Position = F.Npc->GetActorLocation();
        Timer = FYUFSEverydayIntegrationTestAccess::Timer(F.Npc);
        Random = FYUFSEverydayIntegrationTestAccess::RandomState(F.Npc);
        TestFalse(TEXT("Automatic movement cannot add a frame-dependent extra step"),
            F.Npc->GetCharacterMovement()->IsComponentTickEnabled());
    };
    for (bool bWalk : {false, true})
    {
        FVector Reference; float ReferenceTimer; int32 ReferenceRandom;
        Run(300, 1.f/60.f, bWalk, Reference, ReferenceTimer, ReferenceRandom);
        if (bWalk) TestTrue(TEXT("The corridor test actually moves the character"), Reference.X > 100.f);
        for (int32 Fps : {30, 120})
        {
            FVector Actual; float Timer; int32 Random;
            Run(Fps*5, 1.f/Fps, bWalk, Actual, Timer, Random);
            TestTrue(TEXT("Position matches at equal simulated time"), Actual.Equals(Reference, .01f));
            TestEqual(TEXT("Activity clock matches across frame rates"), Timer, ReferenceTimer);
            TestEqual(TEXT("Seeded draw consumption matches across frame rates"), Random, ReferenceRandom);
        }
    }
    return true;
}

#endif
