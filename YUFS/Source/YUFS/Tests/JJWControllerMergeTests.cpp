#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "Simulation/YUFSSimulationController.h"

// Exercise controller bookkeeping without loading assets or simulating a fire.
struct FYUFSJJWControllerTestAccess
{
    static void Evacuate(AYUFSSimulationController* Controller, AYUFSEvacuationNPC* NPC)
    { Controller->ResolvedNPCs.Add(NPC); ++Controller->LiveEvacuatedCount; }
    static void Incapacitate(AYUFSSimulationController* Controller, AYUFSEvacuationNPC* NPC)
    { Controller->ResolvedNPCs.Add(NPC); ++Controller->LiveIncapacitatedCount; }
    static void Finish(AYUFSSimulationController* Controller) { Controller->FinalizeRun(); }
    static void CheckCompletion(AYUFSSimulationController* Controller) { Controller->CheckCompletionCondition(); }
};

namespace
{
struct FControllerFixture
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    AYUFSSimulationController* Controller = World->SpawnActor<AYUFSSimulationController>();
    FControllerFixture() { Controller->bEnableTimelineRecording = false; }
    ~FControllerFixture() { World->DestroyWorld(false); }
    AYUFSEvacuationNPC* Resident(FVector Position)
    {
        FActorSpawnParameters Params;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* NPC = World->SpawnActor<AYUFSEvacuationNPC>(Position, FRotator::ZeroRotator, Params);
        Controller->RegisterNPC(NPC);
        return NPC;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSJJWManualPlacementTest,
    "YUFS.NPC.Simulation.JJWMerge.ManualPlacementAndDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSJJWManualPlacementTest::RunTest(const FString&)
{
    FControllerFixture F;
    TestFalse(TEXT("Automatic redistribution is off"), F.Controller->bDistributeOverlappingNPCs);
    TestFalse(TEXT("Action gallery is off"), F.Controller->bPreviewAllNPCActionAnimations);
    TestFalse(TEXT("Camera takeover is off"), F.Controller->bAutoFocusNPCActionAnimationShowcase);
    auto* A = F.Resident(FVector(100, 200, 100));
    auto* B = F.Resident(FVector(500, 200, 100));
    const FVector APosition = A->GetActorLocation(), BPosition = B->GetActorLocation();
    F.Controller->RegisterNPC(A);
    TestEqual(TEXT("Duplicate registration cannot inflate palette count"), F.Controller->GetTotalNPCCount(), 2);
    F.Controller->Tick(10.f);
    F.Controller->StartNPCActionAnimationShowcase();
    TestEqual(TEXT("Waiting never starts the run clock"), F.Controller->GetElapsedTime(), 0.f);
    TestEqual(TEXT("Registered manual placement is not redistributed"), A->GetActorLocation(), APosition);
    TestEqual(TEXT("Other manual placement is not redistributed"), B->GetActorLocation(), BPosition);
    TestFalse(TEXT("Deprecated showcase does not hide residents"), B->IsHidden());
    F.Controller->UnregisterNPC(B);
    TestEqual(TEXT("Cancelled placement is immediately removed from count"), F.Controller->GetTotalNPCCount(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSJJWResultCountTest,
    "YUFS.NPC.Simulation.JJWMerge.ResolvedParticipantsCountOnce",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSJJWResultCountTest::RunTest(const FString&)
{
    FControllerFixture F;
    auto* A = F.Resident(FVector(100, 200, 100));
    auto* B = F.Resident(FVector(500, 200, 100));
    F.Controller->StartSimulation();
    FYUFSJJWControllerTestAccess::Evacuate(F.Controller, A);
    FYUFSJJWControllerTestAccess::Incapacitate(F.Controller, B);
    FYUFSJJWControllerTestAccess::Finish(F.Controller);
    FYUFSJJWControllerTestAccess::Finish(F.Controller);
    const auto Results = F.Controller->GetAllRunResults();
    TestEqual(TEXT("A repeated completion does not duplicate the result"), Results.Num(), 1);
    if (Results.Num() == 1)
    {
        TestEqual(TEXT("Retained timeline actors are not counted twice"), Results[0].TotalNPCCount, 2);
        TestEqual(TEXT("One of two participants evacuated"), Results[0].EvacuationRate, .5f);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSJJWMidRunRemovalTest,
    "YUFS.NPC.Simulation.JJWMerge.MidRunRemovalDoesNotBlockCompletion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSJJWMidRunRemovalTest::RunTest(const FString&)
{
    FControllerFixture F;
    auto* A = F.Resident(FVector(100, 200, 100));
    auto* B = F.Resident(FVector(500, 200, 100));
    F.Controller->StartSimulation();
    FYUFSJJWControllerTestAccess::Evacuate(F.Controller, A);
    F.Controller->UnregisterNPC(B);
    TestEqual(TEXT("An unresolved withdrawal is not an extra participant"), F.Controller->GetTotalNPCCount(), 1);
    TestEqual(TEXT("Deletion is not fabricated as incapacitation"), F.Controller->GetIncapacitatedCount(), 0);
    // Removing the already-resolved render actor preserves its historical result.
    F.Controller->UnregisterNPC(A);
    FYUFSJJWControllerTestAccess::CheckCompletion(F.Controller);
    TestEqual(TEXT("Only withdrawn actors cannot keep a run alive forever"), F.Controller->GetCurrentPhase(), ESimPhase::Completed);
    const auto Results = F.Controller->GetAllRunResults();
    TestEqual(TEXT("Completion is recorded"), Results.Num(), 1);
    if (Results.Num() == 1)
    {
        TestEqual(TEXT("Historical success survives render actor removal"), Results[0].EvacuatedCount, 1);
        TestEqual(TEXT("Remaining participant is counted once"), Results[0].TotalNPCCount, 1);
    }
    return true;
}
#endif
