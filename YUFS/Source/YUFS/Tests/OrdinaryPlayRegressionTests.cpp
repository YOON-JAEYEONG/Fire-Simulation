#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Navigation/YUFSLocalMovementComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

struct FYUFSTrafficRegressionAccess
{
    static void Wait(AYUFSEvacuationNPC* A, AYUFSEvacuationNPC* B)
    {
        auto* T = A->GetLocalMovement();
        T->YieldingTo = B;
        T->State = EYUFSLocalMovementState::Yielding;
    }
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSTrafficCycleRegression,
    "YUFS.NPC.Navigation.Traffic.ThreeAgentCycleHasOneLeader",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSTrafficCycleRegression::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    TArray<AYUFSEvacuationNPC*> Actors;
    for (int32 I=0; I<3; ++I) Actors.Add(World->SpawnActor<AYUFSEvacuationNPC>());
    Actors.Sort([](const AYUFSEvacuationNPC& A, const AYUFSEvacuationNPC& B) { return A.GetUniqueID()<B.GetUniqueID(); });
    for (int32 I=0; I<3; ++I) FYUFSTrafficRegressionAccess::Wait(Actors[I], Actors[(I+1)%3]);
    TestTrue(TEXT("One cycle leader proceeds"), Actors[0]->GetLocalMovement()->IsCycleLeader(Actors[1]));
    TestFalse(TEXT("Second resident yields"), Actors[1]->GetLocalMovement()->IsCycleLeader(Actors[2]));
    TestFalse(TEXT("Third resident yields"), Actors[2]->GetLocalMovement()->IsCycleLeader(Actors[0]));
    Actors[2]->GetLocalMovement()->Reset();
    TestFalse(TEXT("An ordinary queue is not a cycle"), Actors[0]->GetLocalMovement()->IsCycleLeader(Actors[1]));
    World->DestroyWorld(false);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSResolvedAvoidanceRegression,
    "YUFS.NPC.Navigation.Traffic.EvacuatedResidentStopsAvoidance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSResolvedAvoidanceRegression::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    auto* Npc = World->SpawnActor<AYUFSEvacuationNPC>();
    Npc->GetCharacterMovement()->Velocity = FVector(100,0,0);
    Npc->NotifyEpisodeFinished(EYUFSTerminalReason::ReachedExit);
    TestFalse(TEXT("Resolved NPC cannot remain an RVO obstacle"), Npc->GetCharacterMovement()->bUseRVOAvoidance);
    TestTrue(TEXT("Resolved NPC has no residual velocity"), Npc->GetVelocity().IsNearlyZero());
    TestEqual(TEXT("Component movement stops even before actor tick is disabled"), Npc->GetCharacterMovement()->MovementMode.GetValue(), MOVE_None);
    Npc->SetTimelinePlaybackMode(true);
    Npc->SetTimelinePlaybackMode(false);
    TestTrue(TEXT("Resuming live mode restores avoidance"), Npc->GetCharacterMovement()->bUseRVOAvoidance);
    World->DestroyWorld(false);
    return true;
}
#endif
