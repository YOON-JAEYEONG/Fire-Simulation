#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "NPC/Behavior/YUFSBehaviorConfig.h"
#include "NPC/Cognition/YUFSHumanCognitionComponent.h"
#include "NPC/Decision/YUFSIntentComponent.h"
#include "NPC/Integration/YUFSTeamIntegrationComponent.h"
#include "NPC/Tasks/YUFSActionTaskComponent.h"

// The bridge is exercised through its real private entry points. This friendship
// changes only test visibility: no state-machine fields, actions or outcomes are set.
struct FYUFSJJWDecisionBridgeTestAccess
{
    static void Initialize(AYUFSEvacuationNPC* Npc, int32 Seed)
    {
        Npc->StableNPCId=Seed;
        Npc->bLogTransitions=Npc->bLogDecisionTrace=false;
        Npc->bDataCollectionMode=true; // Real rule fallback, with no model/file load.
        Npc->bUseExternalNavigationDriver=Npc->bUseExternalMotionDriver=true;
        Npc->DeterministicRng.Initialize(173,Seed);
        Npc->MLPolicy.SetFallbackRandomSource(&Npc->DeterministicRng);
        Npc->HumanCognitionComp->Initialize(Seed,Npc->DeterministicRng);
        auto* State=Npc->GetBehaviorStateMachine();
        State->Config=NewObject<UYUFSBehaviorConfig>(State);
        State->Config->EarlyAlarmResponseFraction=1.f;
        State->Config->VerifyAlarmResponseFraction=0.f;
        State->Config->PreparationDuration=1000.f;
        State->InitializePersonality(Seed);
    }
    static void BridgeAndPolicy(AYUFSEvacuationNPC* Npc, FYUFSNPCObservation& O, float Dt)
    {
        O.CurrentState=Npc->BehaviorSM->GetCurrentState();
        O.RiskPerception=Npc->BehaviorSM->GetRiskPerception();
        O.SmokeExposureAccumulated=Npc->BehaviorSM->GetSmokeExposure();
        Npc->UpdateEvidenceDecisionModel(Dt,O);
        Npc->LiveObservation=O;
        Npc->TickPolicy(Dt,O);
    }
    static void TickObservedDecision(AYUFSEvacuationNPC* Npc, FYUFSNPCObservation& O, float Dt)
    {
        Npc->BehaviorSM->TickStateMachine(Dt,O);
        BridgeAndPolicy(Npc,O,Dt);
    }
    static void StartFreshActionHold(AYUFSEvacuationNPC* Npc)
    {
        Npc->ActionHoldTimer=0.f;
        Npc->PolicyTickAccumulator=0.f;
        Npc->LastPolicyBehaviorState=Npc->BehaviorSM->GetCurrentState();
    }
    static float GetActionHoldTime(const AYUFSEvacuationNPC* Npc) { return Npc->ActionHoldTimer; }
    static bool CompleteGatherPresentation(AYUFSEvacuationNPC* Npc)
    {
        auto* Tasks=Npc->ActionTaskComp.Get();
        Tasks->GatherBelongingsDuration={.1f,0.f,.1f};
        Tasks->UpdateDesiredTask(0.f,EYUFSActionTask::GatherBelongings,1,
            EYUFSIntent::Prepare,false,false,Npc->DeterministicRng);
        Tasks->UpdateDesiredTask(.2f,EYUFSActionTask::GatherBelongings,1,
            EYUFSIntent::Prepare,false,false,Npc->DeterministicRng);
        EYUFSActionTask From=EYUFSActionTask::None, To=EYUFSActionTask::None;
        EYUFSTaskCancelReason Reason=EYUFSTaskCancelReason::None;
        bool bCompleted=false;
        while (Tasks->ConsumeTaskEvent(From,To,Reason))
            bCompleted|=From==EYUFSActionTask::GatherBelongings && Reason==EYUFSTaskCancelReason::Completed;
        return bCompleted;
    }
};

namespace
{
struct FJJWBridgeWorld
{
    UWorld* World=nullptr;
    AYUFSEvacuationNPC* Npc=nullptr;
    FJJWBridgeWorld()
    {
        const auto Settings=UWorld::InitializationValues().AllowAudioPlayback(false)
            .CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
        World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Settings);
        if (!World) return;
        FActorSpawnParameters Spawn;
        Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Npc=World->SpawnActor<AYUFSEvacuationNPC>(FVector(0,0,90),FRotator::ZeroRotator,Spawn);
        if (Npc) FYUFSJJWDecisionBridgeTestAccess::Initialize(Npc,37);
    }
    ~FJJWBridgeWorld() { if (World) World->DestroyWorld(false); }
    bool EnterPreparation(FYUFSNPCObservation& O)
    {
        O.bAlarmSounding=true;
        // AlarmDecisionTime remains the seeded JJW 3..9 second sample. There is
        // no direct state write or assumption that all agents respond immediately.
        for (int32 Tick=0;Tick<120 && !Npc->GetBehaviorStateMachine()->HasCommittedToEvacuation();++Tick)
            FYUFSJJWDecisionBridgeTestAccess::TickObservedDecision(Npc,O,.1f);
        return Npc->GetBehaviorStateMachine()->GetCurrentState()==EYUFSBehaviorState::Preparing;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJWDecisionBridgePreparationTest,
    "YUFS.NPC.Integration.JJWDecisionBridge.PreparationRejectsLegacyIntentWrites",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)
bool FJJWDecisionBridgePreparationTest::RunTest(const FString&)
{
    FJJWBridgeWorld Fixture;
    if (!TestNotNull(TEXT("asset-free bridge NPC"),Fixture.Npc)) return false;
    auto* Npc=Fixture.Npc;
    auto* State=Npc->GetBehaviorStateMachine();
    auto* Legacy=Npc->GetIntentComponent();
    FYUFSNPCObservation O;
    Legacy->ApplyAuthoritativeIntent(EYUFSIntent::Shelter);
    TestEqual(TEXT("a legacy Shelter write cannot change the live uncommitted intent"),Npc->GetCurrentIntent(),EYUFSIntent::Observe);
    FYUFSJJWDecisionBridgeTestAccess::BridgeAndPolicy(Npc,O,.1f);
    TestEqual(TEXT("real bridge repairs the legacy projection from JJW"),Legacy->GetCurrentIntent(),EYUFSIntent::Observe);
    TestFalse(TEXT("legacy writes do not commit an unaware NPC"),State->HasCommittedToEvacuation());
    TestEqual(TEXT("real policy remains idle without a cue"),Npc->GetLastAction(),EYUFSAction::Idle);

    if (!TestTrue(TEXT("actual alarm observation enters JJW's seeded preparation stage"),Fixture.EnterPreparation(O))) return false;
    TestEqual(TEXT("preparation is the live intent"),Npc->GetCurrentIntent(),EYUFSIntent::Prepare);
    TestEqual(TEXT("real policy publishes gathering during JJW preparation"),Npc->GetLastAction(),EYUFSAction::GatherBelongings);
    const float RiskBeforeBridge=State->GetRiskPerception();
    Legacy->ApplyAuthoritativeIntent(EYUFSIntent::Shelter);
    TestEqual(TEXT("legacy Shelter cannot override preparing state"),Npc->GetCurrentIntent(),EYUFSIntent::Prepare);
    O.StressLevel=.95f; // The fallback would propose warning; the JJW readiness guard still wins.
    FYUFSJJWDecisionBridgeTestAccess::BridgeAndPolicy(Npc,O,.1f);
    TestEqual(TEXT("old cognition/intent bridge does not write JJW perceived risk"),State->GetRiskPerception(),RiskBeforeBridge);
    TestEqual(TEXT("authoritative preparation overrides an incompatible raw action"),Npc->GetLastAction(),EYUFSAction::GatherBelongings);
    TestEqual(TEXT("motion consumer receives the same gathering action"),Npc->GetTeamIntegrationComponent()->GetMotionDirective().LegacyAction,EYUFSAction::GatherBelongings);
    TestEqual(TEXT("motion consumer receives the authoritative state"),Npc->GetTeamIntegrationComponent()->GetMotionDirective().BehaviorState,EYUFSBehaviorState::Preparing);

    TestTrue(TEXT("the actual presentation task can finish independently"),FYUFSJJWDecisionBridgeTestAccess::CompleteGatherPresentation(Npc));
    Legacy->MaxPreActionLoopGuard=1;
    Legacy->NotifyPreActionCompleted(true);
    TestEqual(TEXT("fixture exercises the old completion path that requests evacuation"),Legacy->GetCurrentIntent(),EYUFSIntent::CommitEvac);
    TestEqual(TEXT("old task completion cannot shorten JJW preparation"),State->GetCurrentState(),EYUFSBehaviorState::Preparing);
    TestEqual(TEXT("public live intent ignores that old task write"),Npc->GetCurrentIntent(),EYUFSIntent::Prepare);
    FYUFSJJWDecisionBridgeTestAccess::BridgeAndPolicy(Npc,O,.01f);
    TestEqual(TEXT("next real bridge restores the authoritative projection"),Legacy->GetCurrentIntent(),EYUFSIntent::Prepare);
    TestEqual(TEXT("presentation completion cannot force the runtime action into escape"),Npc->GetLastAction(),EYUFSAction::GatherBelongings);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJWDecisionBridgeEmergencyTest,
    "YUFS.NPC.Integration.JJWDecisionBridge.EmergencyBypassesActionHold",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)
bool FJJWDecisionBridgeEmergencyTest::RunTest(const FString&)
{
    FJJWBridgeWorld Fixture;
    if (!TestNotNull(TEXT("asset-free emergency bridge NPC"),Fixture.Npc)) return false;
    auto* Npc=Fixture.Npc;
    auto* State=Npc->GetBehaviorStateMachine();
    FYUFSNPCObservation O;
    if (!TestTrue(TEXT("real alarm policy starts in preparation"),Fixture.EnterPreparation(O))) return false;
    TestEqual(TEXT("the action before danger is gathering"),Npc->GetLastAction(),EYUFSAction::GatherBelongings);
    FYUFSJJWDecisionBridgeTestAccess::StartFreshActionHold(Npc);
    Npc->GetIntentComponent()->ApplyAuthoritativeIntent(EYUFSIntent::Shelter);
    O.NearbyHeat=O.NearbyHeatNormalized=State->Config->EmergencyHeatThreshold;
    O.bHazardSampleAvailable=true;
    constexpr float EmergencyTick=.001f;
    FYUFSJJWDecisionBridgeTestAccess::TickObservedDecision(Npc,O,EmergencyTick);
    TestEqual(TEXT("actual observed heat makes JJW immediately evacuate"),State->GetCurrentState(),EYUFSBehaviorState::Evacuating);
    TestEqual(TEXT("the observed cause remains heat"),State->GetDecisionCue(),EYUFSEvacuationCue::Heat);
    TestTrue(TEXT("commitment survives the legacy Shelter write"),State->HasCommittedToEvacuation());
    TestEqual(TEXT("live intent and bridge agree on evacuation"),Npc->GetCurrentIntent(),EYUFSIntent::CommitEvac);
    TestEqual(TEXT("legacy projection is corrected on the same tick"),Npc->GetIntentComponent()->GetCurrentIntent(),EYUFSIntent::CommitEvac);
    TestEqual(TEXT("real TickPolicy starts evacuation without waiting for the 2 second hold"),Npc->GetLastAction(),EYUFSAction::EvacuateToNearestExit);
    TestTrue(TEXT("regression exercises a sub-policy-interval tick, not elapsed hold time"),FYUFSJJWDecisionBridgeTestAccess::GetActionHoldTime(Npc)<.1f);
    TestEqual(TEXT("motion directive immediately exposes the evacuation action"),Npc->GetTeamIntegrationComponent()->GetMotionDirective().LegacyAction,EYUFSAction::EvacuateToNearestExit);
    TestEqual(TEXT("motion directive uses JJW emergency state"),Npc->GetTeamIntegrationComponent()->GetMotionDirective().BehaviorState,EYUFSBehaviorState::Evacuating);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJWDecisionBridgeHelpingTest,
    "YUFS.NPC.Integration.JJWDecisionBridge.PreservesAuthoritativeHelpingAction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)
bool FJJWDecisionBridgeHelpingTest::RunTest(const FString&)
{
    FJJWBridgeWorld Fixture;
    if (!TestNotNull(TEXT("asset-free helping bridge NPC"),Fixture.Npc)) return false;
    auto* Npc=Fixture.Npc;
    FYUFSNPCObservation O;
    O.SmokeInFrontNormalized=.60f;
    O.bHazardSampleAvailable=true;
    for (int32 Tick=0; Tick<10 && !Npc->GetBehaviorStateMachine()->HasCommittedToEvacuation(); ++Tick)
        FYUFSJJWDecisionBridgeTestAccess::TickObservedDecision(Npc,O,.1f);
    TestTrue(TEXT("direct sight leads to actual JJW commitment"),Npc->GetBehaviorStateMachine()->HasCommittedToEvacuation());
    O.SmokeInFrontNormalized=0.f;
    O.bNearbyNPCNeedsHelp=true;
    FYUFSJJWDecisionBridgeTestAccess::TickObservedDecision(Npc,O,.1f);
    TestEqual(TEXT("JJW decides to help on low-risk observed need"),Npc->GetBehaviorStateMachine()->GetCurrentState(),EYUFSBehaviorState::Helping);
    TestEqual(TEXT("the integration bridge does not erase JJW's helping action"),Npc->GetLastAction(),EYUFSAction::HelpOther);
    TestEqual(TEXT("motion driver receives the same native helping action"),Npc->GetTeamIntegrationComponent()->GetMotionDirective().LegacyAction,EYUFSAction::HelpOther);
    return true;
}

#endif
