#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Level/YUFSLevelDataManager.h"
#include "Level/YUFSExitPoint.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "Core/YUFSObservation.h"

struct FYUFSExitValidationTestAccess
{
    static void SetManager(AYUFSEvacuationNPC* NPC, AYUFSLevelDataManager* Manager) { NPC->LevelDataMgr = Manager; }
    static EYUFSTerminalReason Terminal(AYUFSEvacuationNPC* NPC) { return NPC->GetCurrentTerminalReason(); }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSExitValidationTest,
    "YUFS.NPC.Navigation.ExitValidation.MissingDestroyedAndOrigin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSExitValidationTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    auto* Manager = World->SpawnActor<AYUFSLevelDataManager>();
    auto* NPC = World->SpawnActor<AYUFSEvacuationNPC>();
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
