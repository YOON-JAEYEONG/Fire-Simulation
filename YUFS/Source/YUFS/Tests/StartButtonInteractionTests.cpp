#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Engine/World.h"
#include "Simulation/YUFSSimulationController.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FYUFSStartButtonLifecycleTest,
    "YUFS.NPC.Interaction.StartButtonLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FYUFSStartButtonLifecycleTest::RunTest(const FString& Parameters)
{
    // Preview readiness depends on the actual map's asynchronous distribution.
    // Exercise that path in PIE; this test deliberately has no map or BeginPlay.
    if (FParse::Param(FCommandLine::Get(), TEXT("YUFSBuildingInteractions"))
        && FParse::Param(FCommandLine::Get(), TEXT("YUFSInteractionPreview")))
    {
        AddInfo(TEXT("Asset-free lifecycle test skipped under preview launch flags; run without those flags."));
        return true;
    }

    const auto Settings = UWorld::InitializationValues()
        .AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr,
        true, ERHIFeatureLevel::Num, &Settings);
    if (!TestNotNull(TEXT("isolated test world created"), World)) return false;

    AYUFSSimulationController* Controller = World->SpawnActor<AYUFSSimulationController>();
    if (!TestNotNull(TEXT("controller spawned without BeginPlay"), Controller))
    {
        World->DestroyWorld(false);
        return false;
    }
    Controller->bPreviewAllNPCActionAnimations = false;
    Controller->bEnableTimelineRecording = false;
    Controller->FireStartDelaySeconds = 2.f;
    Controller->MaxSimDurationSeconds = 300.f;

    TestTrue(TEXT("initial state waits for Start"), Controller->GetCurrentPhase() == ESimPhase::WaitingToStart);
    Controller->Tick(.5f);
    TestEqual(TEXT("waiting does not run the clock"), Controller->GetElapsedTime(), 0.f);
    TestFalse(TEXT("waiting does not enable NPC simulation"), Controller->IsNPCSimulationEnabled());

    Controller->StartSimulation();
    TestTrue(TEXT("Start enters the fire countdown"), Controller->GetCurrentPhase() == ESimPhase::FireStartDelay);
    Controller->Tick(.5f);
    TestEqual(TEXT("countdown advances after Start"), Controller->GetElapsedTime(), .5f);
    Controller->StartSimulation();
    TestEqual(TEXT("duplicate Start does not reset time"), Controller->GetElapsedTime(), .5f);
    TestEqual(TEXT("duplicate Start does not create another run"), Controller->GetCurrentRunIndex(), 1);

    Controller->PauseSimulation();
    Controller->PauseSimulation();
    Controller->Tick(4.f);
    TestEqual(TEXT("paused countdown does not advance"), Controller->GetElapsedTime(), .5f);
    TestTrue(TEXT("paused countdown cannot ignite"), Controller->GetCurrentPhase() == ESimPhase::FireStartDelay);
    Controller->ResumeSimulation();
    Controller->ResumeSimulation();
    TestEqual(TEXT("Resume does not restart the run"), Controller->GetElapsedTime(), .5f);
    Controller->Tick(1.5f);
    TestTrue(TEXT("remaining countdown ignites after Resume"), Controller->GetCurrentPhase() == ESimPhase::FireActive);
    TestTrue(TEXT("active simulation enables NPC execution"), Controller->IsNPCSimulationEnabled());
    TestEqual(TEXT("only unpaused time is accumulated"), Controller->GetElapsedTime(), 2.f);

    Controller->PauseSimulation();
    TestFalse(TEXT("Pause disables NPC execution"), Controller->IsNPCSimulationEnabled());
    Controller->StartSimulation();
    Controller->Tick(1.f);
    TestEqual(TEXT("duplicate Start cannot reset a paused active run"), Controller->GetElapsedTime(), 2.f);
    TestFalse(TEXT("paused Start is not an implicit Resume"), Controller->IsNPCSimulationEnabled());
    Controller->ResumeSimulation();
    Controller->Tick(.25f);
    TestTrue(TEXT("Resume reenables NPC execution"), Controller->IsNPCSimulationEnabled());
    TestEqual(TEXT("active clock continues without restarting"), Controller->GetElapsedTime(), 2.25f);
    Controller->StartSimulation();
    TestEqual(TEXT("active Start remains idempotent"), Controller->GetElapsedTime(), 2.25f);

    World->DestroyWorld(false);
    return true;
}

#endif
