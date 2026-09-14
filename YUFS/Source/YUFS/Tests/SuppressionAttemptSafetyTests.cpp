#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/YUFSObservation.h"
#include "NPC/Integration/YUFSSuppressionSafety.h"
#include "Fire/YUFSHeterogeneousVolume.h"
#include "Engine/World.h"
#include "Components/ActorComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Integration/YUFSNpcSuppressionComponent.h"
#include "NPC/Integration/YUFSTeamIntegrationComponent.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "NPC/Behavior/YUFSBehaviorConfig.h"
#include "NPC/Perception/YUFSNPCPerceptionComponent.h"
#include "NPC/Decision/YUFSHumanBehaviorSelectorComponent.h"
#include "Core/YUFSDeterministicRng.h"
#include "NPC/Decision/YUFSIntentComponent.h"

namespace
{
FYUFSNPCObservation EligibleSuppressionObservation()
{
    FYUFSNPCObservation O;
    O.CurrentState=EYUFSBehaviorState::Evacuating;
    O.bSuppressionAllowedByBehavior=true;
    return O;
}
void ProjectJjwSuppressionReadiness(FYUFSNPCObservation& O, const UYUFSBehaviorStateMachine* State)
{
    O.CurrentState=State->GetCurrentState();
    O.RiskPerception=State->GetRiskPerception();
    O.bSuppressionAllowedByBehavior=State->Config && State->HasCommittedToEvacuation()
        && (O.CurrentState==EYUFSBehaviorState::Evacuating || O.CurrentState==EYUFSBehaviorState::Helping)
        && !FYUFSSuppressionSafety::ImmediateDanger(O,
            State->Config->SmokeAwarenessThreshold*State->Config->EmergencyOverrideMultiplier,
            State->Config->EmergencyHeatThreshold);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSSuppressionRiskHysteresisTest,
    "YUFS.NPC.Suppression.PersonalRiskAndHysteresis",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FYUFSSuppressionRiskHysteresisTest::RunTest(const FString& Parameters)
{
    FYUFSHumanTraits Trained;
    Trained.FireTraining=.85f;
    FYUFSNPCObservation Observation=EligibleSuppressionObservation();
    FYUFSCognitiveState Cognition;
    const float Start=FYUFSSuppressionSafety::StartRisk(Trained);
    const float Stop=FYUFSSuppressionSafety::StopRisk(Trained);
    TestTrue(TEXT("entry risk is strictly lower than continuation risk"),Start<Stop);
    TestTrue(TEXT("hysteresis band is 0.10"),FMath::IsNearlyEqual(Stop-Start,.10f));
    TestTrue(TEXT("low perceived risk allows starting"),FYUFSSuppressionSafety::CanAttempt(Observation,Cognition,Trained,false));
    TestTrue(TEXT("low perceived risk allows continuation"),FYUFSSuppressionSafety::CanAttempt(Observation,Cognition,Trained,true));

    Cognition.PerceivedRisk=Start-.001f;
    TestTrue(TEXT("just below entry threshold starts"),FYUFSSuppressionSafety::CanAttempt(Observation,Cognition,Trained,false));
    Cognition.PerceivedRisk=Start;
    TestFalse(TEXT("exact entry threshold does not start"),FYUFSSuppressionSafety::CanAttempt(Observation,Cognition,Trained,false));
    TestTrue(TEXT("existing attempt continues at the entry threshold"),FYUFSSuppressionSafety::CanAttempt(Observation,Cognition,Trained,true));
    Cognition.PerceivedRisk=(Start+Stop)*.5f;
    TestFalse(TEXT("hysteresis band cannot start a new attempt"),FYUFSSuppressionSafety::CanAttempt(Observation,Cognition,Trained,false));
    TestTrue(TEXT("hysteresis band does not flap an active attempt"),FYUFSSuppressionSafety::CanAttempt(Observation,Cognition,Trained,true));
    Cognition.PerceivedRisk=Stop-.001f;
    TestTrue(TEXT("just below stop threshold continues"),FYUFSSuppressionSafety::CanAttempt(Observation,Cognition,Trained,true));
    Cognition.PerceivedRisk=Stop;
    TestFalse(TEXT("exact stop threshold aborts"),FYUFSSuppressionSafety::CanAttempt(Observation,Cognition,Trained,true));
    Cognition.PerceivedRisk=.95f;
    TestFalse(TEXT("cognitive perceived risk alone aborts"),FYUFSSuppressionSafety::CanAttempt(Observation,Cognition,Trained,true));
    Cognition.PerceivedRisk=0.f;
    Observation.RiskPerception=.95f;
    TestFalse(TEXT("behavior-state perceived risk alone aborts"),FYUFSSuppressionSafety::CanAttempt(Observation,Cognition,Trained,true));
    Observation.RiskPerception=0.f;
    Observation.RiskLevel=.95f;
    TestFalse(TEXT("observed risk alone aborts"),FYUFSSuppressionSafety::CanAttempt(Observation,Cognition,Trained,true));
    Observation.RiskLevel=.20f; Observation.RiskPerception=.40f; Cognition.PerceivedRisk=.30f;
    TestEqual(TEXT("perceived risk uses the strongest local estimate"),FYUFSSuppressionSafety::PerceivedRisk(Observation,Cognition),.40f);

    FYUFSHumanTraits Cautious=Trained, Tolerant=Trained, MoreTrained=Trained, MoreStressed=Trained;
    Cautious.RiskTolerance=0.f;
    Tolerant.RiskTolerance=1.f;
    MoreTrained.FireTraining=1.f;
    MoreStressed.StressSensitivity=1.f;
    TestTrue(TEXT("risk tolerance changes personal stop threshold"),FYUFSSuppressionSafety::StopRisk(Tolerant)>FYUFSSuppressionSafety::StopRisk(Cautious));
    TestTrue(TEXT("training raises personal stop threshold"),FYUFSSuppressionSafety::StopRisk(MoreTrained)>Stop);
    TestTrue(TEXT("stress sensitivity lowers personal stop threshold"),FYUFSSuppressionSafety::StopRisk(MoreStressed)<Stop);
    Cognition.PerceivedRisk=(FYUFSSuppressionSafety::StopRisk(Cautious)+FYUFSSuppressionSafety::StopRisk(Tolerant))*.5f;
    Observation.RiskLevel=Observation.RiskPerception=0.f;
    TestFalse(TEXT("same perceived danger makes cautious NPC retreat first"),FYUFSSuppressionSafety::CanAttempt(Observation,Cognition,Cautious,true));
    TestTrue(TEXT("same perceived danger can leave tolerant NPC attempting"),FYUFSSuppressionSafety::CanAttempt(Observation,Cognition,Tolerant,true));
    Tolerant.FireTraining=1.f; Tolerant.StressSensitivity=0.f;
    Cautious.FireTraining=0.f; Cautious.StressSensitivity=1.f;
    TestEqual(TEXT("upper stop threshold remains clamped"),FYUFSSuppressionSafety::StopRisk(Tolerant),.72f);
    TestEqual(TEXT("lower stop threshold remains clamped"),FYUFSSuppressionSafety::StopRisk(Cautious),.50f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSSuppressionEmergencyPriorityTest,
    "YUFS.NPC.Suppression.EmergencyAndAuthorityPriority",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FYUFSSuppressionEmergencyPriorityTest::RunTest(const FString& Parameters)
{
    FYUFSHumanTraits Traits;
    Traits.RiskTolerance=Traits.FireTraining=1.f; Traits.StressSensitivity=0.f;
    FYUFSNPCObservation Observation=EligibleSuppressionObservation();
    FYUFSCognitiveState Cognition;
    auto ExpectBlocked=[&](const TCHAR* Label)
    {
        Observation.bSuppressionAllowedByBehavior=!FYUFSSuppressionSafety::ImmediateDanger(Observation);
        TestFalse(FString(Label)+TEXT(" prevents starting"),FYUFSSuppressionSafety::CanAttempt(Observation,Cognition,Traits,false));
        TestFalse(FString(Label)+TEXT(" interrupts an active attempt"),FYUFSSuppressionSafety::CanAttempt(Observation,Cognition,Traits,true));
    };
    Observation.NearbyHeatNormalized=.649f;
    TestFalse(TEXT("near heat below emergency boundary is not an immediate override"),FYUFSSuppressionSafety::ImmediateDanger(Observation));
    Observation.NearbyHeatNormalized=.65f;
    TestTrue(TEXT("normalized nearby heat 0.65 is emergency"),FYUFSSuppressionSafety::ImmediateDanger(Observation));
    ExpectBlocked(TEXT("nearby heat"));
    Observation=EligibleSuppressionObservation(); Observation.TemperatureAtSelf=.65f;
    ExpectBlocked(TEXT("self heat"));
    Observation=EligibleSuppressionObservation(); Observation.SmokeDensityAtSelf=.30f;
    TestFalse(TEXT("JJW self smoke exactly 0.30 retains the strict greater-than boundary"),FYUFSSuppressionSafety::ImmediateDanger(Observation));
    Observation.SmokeDensityAtSelf=.301f;
    TestTrue(TEXT("JJW self smoke over 0.30 is emergency"),FYUFSSuppressionSafety::ImmediateDanger(Observation));
    ExpectBlocked(TEXT("dense smoke"));

    Observation=EligibleSuppressionObservation(); Observation.CurrentState=EYUFSBehaviorState::Incapacitated;
    ExpectBlocked(TEXT("incapacity"));
    Observation.CurrentState=EYUFSBehaviorState::Crawling;
    ExpectBlocked(TEXT("crawling"));
    Observation=EligibleSuppressionObservation(); Observation.bReceivedStaffGuidance=true;
    ExpectBlocked(TEXT("staff guidance"));
    Observation=EligibleSuppressionObservation(); Observation.bReceivedLiveAnnouncement=true;
    ExpectBlocked(TEXT("live official announcement"));
    Observation=EligibleSuppressionObservation(); Cognition.PhysicalSeverity=EYUFSPerceivedPhysicalSeverity::ImmediateLifeThreat;
    ExpectBlocked(TEXT("perceived immediate life threat"));
    Cognition={}; Observation=EligibleSuppressionObservation(); Observation.bAlarmSounding=true;
    TestTrue(TEXT("an already committed evacuee may consider suppression with an alarm sounding"),FYUFSSuppressionSafety::CanAttempt(Observation,Cognition,Traits,false));
    Observation.bReceivedPreRecordedMsg=true;
    TestTrue(TEXT("a recorded cue alone does not bypass personal judgment"),FYUFSSuppressionSafety::CanAttempt(Observation,Cognition,Traits,false));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSSuppressionJjwPriorityTest,
    "YUFS.NPC.Suppression.JjwCommitPreparationAndConfiguredEmergency",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FYUFSSuppressionJjwPriorityTest::RunTest(const FString& Parameters)
{
    auto* State=NewObject<UYUFSBehaviorStateMachine>();
    State->Config=NewObject<UYUFSBehaviorConfig>(State);
    State->Config->RiskPerceptionThreshold=.01f;
    State->Config->PreparationDuration=1000.f;
    State->InitializePersonality(19);
    FYUFSNPCObservation O;
    FYUFSCognitiveState Cognition;
    FYUFSHumanTraits Traits; Traits.FireTraining=1.f;
    ProjectJjwSuppressionReadiness(O,State);
    TestFalse(TEXT("uncommitted JJW state cannot begin suppression"),FYUFSSuppressionSafety::CanAttempt(O,Cognition,Traits,false));
    O.bReceivedPreRecordedMsg=true;
    for (int32 Tick=0; Tick<40; ++Tick) State->TickStateMachine(.1f,O);
    ProjectJjwSuppressionReadiness(O,State);
    TestTrue(TEXT("real corroborated observation leads to JJW commitment"),State->HasCommittedToEvacuation());
    TestEqual(TEXT("JJW's preparation stage is preserved"),O.CurrentState,EYUFSBehaviorState::Preparing);
    TestFalse(TEXT("being committed does not bypass unfinished preparation"),FYUFSSuppressionSafety::CanAttempt(O,Cognition,Traits,false));
    TestFalse(TEXT("unfinished preparation also blocks continuation"),FYUFSSuppressionSafety::CanAttempt(O,Cognition,Traits,true));
    State->Config->PreparationDuration=0.f;
    State->TickStateMachine(.1f,O);
    ProjectJjwSuppressionReadiness(O,State);
    TestEqual(TEXT("JJW itself completes preparation"),O.CurrentState,EYUFSBehaviorState::Evacuating);
    TestTrue(TEXT("low-risk committed evacuee can then consider a tool"),FYUFSSuppressionSafety::CanAttempt(O,Cognition,Traits,false));

    State->Config->SmokeAwarenessThreshold=.20f;
    State->Config->EmergencyOverrideMultiplier=2.f;
    State->Config->EmergencyHeatThreshold=.80f;
    O.SmokeDensityAtSelf=.35f;
    ProjectJjwSuppressionReadiness(O,State);
    TestTrue(TEXT("custom JJW smoke threshold is not overwritten by the old fixed 0.30 gate"),FYUFSSuppressionSafety::CanAttempt(O,Cognition,Traits,false));
    O.SmokeDensityAtSelf=.401f;
    ProjectJjwSuppressionReadiness(O,State);
    TestFalse(TEXT("configured smoke emergency blocks a pending attempt"),FYUFSSuppressionSafety::CanAttempt(O,Cognition,Traits,false));
    TestFalse(TEXT("configured smoke emergency cancels an active attempt"),FYUFSSuppressionSafety::CanAttempt(O,Cognition,Traits,true));
    O.SmokeDensityAtSelf=0.f; O.NearbyHeat=O.NearbyHeatNormalized=.79f;
    ProjectJjwSuppressionReadiness(O,State);
    TestTrue(TEXT("configured nearby heat boundary is used without a hidden fixed override"),FYUFSSuppressionSafety::CanAttempt(O,Cognition,Traits,false));
    O.NearbyHeat=O.NearbyHeatNormalized=.80f;
    ProjectJjwSuppressionReadiness(O,State);
    TestFalse(TEXT("configured inclusive nearby heat boundary blocks continuation"),FYUFSSuppressionSafety::CanAttempt(O,Cognition,Traits,true));

    auto* Selector=NewObject<UYUFSHumanBehaviorSelectorComponent>();
    Selector->SuppressionProbabilityOverride=1.f;
    Selector->bDemonstrateSuppressionWhenEligible=true;
    FYUFSInteractionOpportunitySnapshot Opportunity;
    Opportunity.bExtinguisherKnownAvailable=Opportunity.bSuppressibleFireKnown=Opportunity.bSafeRetreatKnown=true;
    Opportunity.ExtinguisherStableId=TEXT("KnownTool"); Opportunity.FireStableId=TEXT("FdsSource");
    FYUFSDeterministicRngSet Rng; Rng.Initialize(173,19);
    O={}; O.bAlarmSounding=true;
    const auto Before=Selector->ResolveDecision(EYUFSAction::Idle,EYUFSIntent::Observe,O,Cognition,Traits,Opportunity,false,Rng);
    TestTrue(TEXT("even 100 percent demonstration cannot bypass JJW's uncommitted state"),Before.Behavior!=EYUFSHighLevelBehavior::AttemptSuppression);
    O.CurrentState=EYUFSBehaviorState::Preparing;
    const auto Preparing=Selector->ResolveDecision(EYUFSAction::GatherBelongings,EYUFSIntent::Prepare,O,Cognition,Traits,Opportunity,false,Rng);
    TestTrue(TEXT("probability override cannot turn preparation into suppression"),Preparing.Behavior!=EYUFSHighLevelBehavior::AttemptSuppression);
    O=EligibleSuppressionObservation(); O.bAlarmSounding=true;
    const auto After=Selector->ResolveDecision(EYUFSAction::EvacuateToNearestExit,EYUFSIntent::CommitEvac,O,Cognition,Traits,Opportunity,false,Rng);
    TestEqual(TEXT("the same configured choice is available only once JJW is ready"),After.Behavior,EYUFSHighLevelBehavior::AttemptSuppression);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSSuppressionObservationContractTest,
    "YUFS.NPC.Suppression.RuntimeHeatPreserves28Inputs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FYUFSSuppressionObservationContractTest::RunTest(const FString& Parameters)
{
    FYUFSNPCObservation Observation;
    Observation.SmokeDensityAtSelf=.11f; Observation.TemperatureAtSelf=.12f;
    Observation.SmokeInFrontNormalized=.13f; Observation.SmokeAboveNormalized=.14f;
    Observation.RiskLevel=.15f; Observation.SimTimeNormalized=.16f;
    Observation.DistToNearestExit=2000.f; Observation.DistToFamiliarExit=3000.f;
    Observation.DirToNearestExit=FVector(.4f,.5f,.6f); Observation.bNearestExitSmokeFree=true;
    Observation.NearbyEvacuatingRatio=.7f; Observation.NearbyNPCCount=8; Observation.GroupSize=6;
    Observation.bNearbyNPCNeedsHelp=true; Observation.bAlarmSounding=true;
    Observation.bReceivedPreRecordedMsg=true; Observation.bReceivedLiveAnnouncement=true; Observation.bReceivedStaffGuidance=true;
    Observation.StaffGuidedExitLocation=FVector(1000,2000,3000);
    Observation.CurrentState=EYUFSBehaviorState::Normal; Observation.RiskPerception=.24f;
    Observation.StressLevel=.25f; Observation.MillingActionCount=5; Observation.SmokeExposureAccumulated=.27f;
    const TArray<float> Expected={.11f,.12f,.13f,.14f,.15f,.16f,.2f,.3f,.4f,.5f,.6f,1.f,.7f,.4f,.3f,
        1.f,1.f,1.f,1.f,1.f,.1f,.2f,.3f,0.f,.24f,.25f,.25f,.27f};
    TArray<float> Before;
    Before.Init(-123.f,100);
    Observation.FillFloatArray(Before);
    TestEqual(TEXT("public model contract is exactly 28 inputs"),FYUFSNPCObservation::FeatureCount,28);
    TestEqual(TEXT("FillFloatArray resizes an existing array to 28"),Before.Num(),28);
    if (Before.Num()!=Expected.Num()) return false;
    for (int32 Index=0;Index<Expected.Num();++Index)
        TestTrue(FString::Printf(TEXT("legacy feature %d retains position/value (actual=%.9f expected=%.9f)"),Index,Before[Index],Expected[Index]),
            FMath::IsNearlyEqual(Before[Index],Expected[Index],1.e-6f));
    Observation.HeatInSightNormalized=.98f;
    Observation.NearbyHeatNormalized=.99f;
    Observation.bHazardSampleAvailable=true;
    Observation.bSuppressionAllowedByBehavior=true;
    TArray<float> After;
    Observation.FillFloatArray(After);
    TestEqual(TEXT("runtime-only evidence never extends the model input"),After.Num(),28);
    if (After.Num()!=Before.Num()) return false;
    for (int32 Index=0;Index<Before.Num();++Index)
        TestEqual(FString::Printf(TEXT("runtime heat leaves feature %d unchanged"),Index),After[Index],Before[Index]);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSSuppressionNoInventedFireTest,
    "YUFS.NPC.Suppression.NoLocalFireEffectsOrFallbackTarget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FYUFSSuppressionNoInventedFireTest::RunTest(const FString& Parameters)
{
    const auto Settings=UWorld::InitializationValues().AllowAudioPlayback(false)
        .CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Settings);
    if (!TestNotNull(TEXT("isolated recorded-fire world"),World)) return false;
    auto* Volume=World->SpawnActor<AYUFSHeterogeneousVolume>();
    if (!TestNotNull(TEXT("recorded FDS playback actor"),Volume)) { World->DestroyWorld(false); return false; }
    // No BeginPlay/file loading in this asset-free test. Legacy serialized flags
    // must not become an unverified target when FDS metadata has not loaded.
    Volume->bHasInteractionTarget=true;
    Volume->InteractionTargetLocal=FVector(123,456,789);
    TArray<UActorComponent*> Initial;
    Volume->GetComponents(Initial);
    auto CheckComponents=[&](const TCHAR* Phase)
    {
        TArray<UActorComponent*> Components;
        Volume->GetComponents(Components);
        TestEqual(FString(Phase)+TEXT(" does not create additional components"),Components.Num(),Initial.Num());
        TestNotNull(FString(Phase)+TEXT(" preserves original heterogeneous volume"),Volume->FindComponentByClass<UHeterogeneousVolumeComponent>());
        TestNull(FString(Phase)+TEXT(" has no local point light"),Volume->FindComponentByClass<UPointLightComponent>());
        TestNull(FString(Phase)+TEXT(" has no invented static-mesh flame/smoke planes"),Volume->FindComponentByClass<UStaticMeshComponent>());
        for (const auto* Component:Components)
            TestFalse(FString(Phase)+TEXT(" has no LocalEffect component"),Component->GetName().Contains(TEXT("LocalEffect")));
        FVector Target(999,999,999);
        TestFalse(FString(Phase)+TEXT(" rejects missing FDS target metadata"),Volume->GetInteractionTarget(Target));
        TestTrue(FString(Phase)+TEXT(" invalid target is cleared, not stale legacy coordinates"),Target.IsZero());
    };
    CheckComponents(TEXT("Construction"));
    for (int32 Cycle=0;Cycle<3;++Cycle)
    {
        Volume->StartFire();
        TestTrue(TEXT("Start only starts original recorded playback"),Volume->IsPlaying());
        TestFalse(TEXT("playback alone cannot claim a validated ignition target"),Volume->IsFdsFireActive());
        CheckComponents(TEXT("StartFire"));
        Volume->PauseFire();
        TestFalse(TEXT("Pause stops playback"),Volume->IsPlaying());
        Volume->ResumeFire();
        TestTrue(TEXT("Resume restarts playback"),Volume->IsPlaying());
        Volume->SetFrame(23);
        TestEqual(TEXT("timeline seeks original recording frame"),Volume->GetFrame(),23);
        CheckComponents(TEXT("Seek"));
        Volume->ResetFire();
        TestFalse(TEXT("Reset stops recorded playback"),Volume->IsPlaying());
        TestEqual(TEXT("Reset restores recorded frame zero"),Volume->GetFrame(),0);
        CheckComponents(TEXT("ResetFire"));
    }
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSSuppressionCancellationIntegrationTest,
    "YUFS.NPC.Suppression.CancellationRestoresNativeExecution",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FYUFSSuppressionCancellationIntegrationTest::RunTest(const FString& Parameters)
{
    const auto Settings=UWorld::InitializationValues().AllowAudioPlayback(false)
        .CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Settings);
    if (!TestNotNull(TEXT("isolated suppression cancellation world"),World)) return false;
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Npc=World->SpawnActor<AYUFSEvacuationNPC>(FVector(0,0,90),FRotator::ZeroRotator,Spawn);
    auto* LifecycleNpc=World->SpawnActor<AYUFSEvacuationNPC>(FVector(500,0,90),FRotator::ZeroRotator,Spawn);
    auto* IncapacitatedNpc=World->SpawnActor<AYUFSEvacuationNPC>(FVector(1000,0,90),FRotator::ZeroRotator,Spawn);
    auto* RecordedFire=World->SpawnActor<AYUFSHeterogeneousVolume>();
    if (!Npc || !LifecycleNpc || !IncapacitatedNpc || !RecordedFire)
    { AddError(TEXT("suppression fixtures failed to spawn")); World->DestroyWorld(false); return false; }
    RecordedFire->StartFire(); RecordedFire->SetFrame(17); RecordedFire->ResumeFire();

    // No BeginPlay, external files, NavMesh, or invented ignition metadata. Enter
    // the already-active executor through the test friendship, then exercise
    // the real safety -> Finish -> NPC control handoff implementation.
    auto ArmAttempt=[&](AYUFSEvacuationNPC* Owner)
    {
        auto* Executor=Owner->GetSuppressionComponent();
        Executor->Npc=Owner; Executor->Fire=RecordedFire;
        Executor->bActive=Executor->bSpraying=Executor->bHasAttackPoint=true;
        Executor->bRetreatReachable=false;
        auto* State=Owner->GetBehaviorStateMachine();
        if (!State->Config)
        {
            State->Config=NewObject<UYUFSBehaviorConfig>(State);
            State->InitializePersonality(10);
        }
        FYUFSNPCObservation VisibleSmoke;
        VisibleSmoke.SmokeInFrontNormalized=.60f;
        for (int32 Tick=0; Tick<10; ++Tick) State->TickStateMachine(.1f,VisibleSmoke);
        ProjectJjwSuppressionReadiness(VisibleSmoke,State);
        Executor->LatestObservation=VisibleSmoke;
        Owner->SetMovementSpeed(400.f);
        Owner->bUseExternalNavigationDriver=false;
        Owner->GetCharacterMovement()->MaxWalkSpeed=180.f;
        auto* Team=Owner->GetTeamIntegrationComponent();
        auto Snapshot=Team->GetInteractionOpportunities();
        Snapshot.bHoldingExtinguisher=Snapshot.bSuppressibleFireKnown=Snapshot.bSuppressionApproachKnown=true;
        Snapshot.SuppressionApproachLocation=FVector(30,40,50);
        Snapshot.FireStableId=RecordedFire->GetFName();
        ++Snapshot.KnowledgeRevision; Team->SubmitInteractionOpportunities(Snapshot);
        Executor->ExecutionRevision=Team->GetInteractionDirective().Revision;
        return Executor;
    };
    auto* Suppression=ArmAttempt(Npc);
    // Accumulate actual JJW eyewitness evidence. Never write risk or commitment directly.
    FYUFSNPCObservation PersistentEvidence;
    PersistentEvidence.SmokeInFrontNormalized=.60f;
    for (int32 Tick=0; Tick<100; ++Tick) Npc->GetBehaviorStateMachine()->TickStateMachine(.1f,PersistentEvidence);
    TestTrue(TEXT("JJW evidence accumulation produced genuinely high personal risk"),Npc->GetBehaviorStateMachine()->GetRiskPerception()>.90f);
    const float ExpectedJjwSpeed=Npc->GetDesiredWalkingSpeed();
    const FVector OriginalLocation=Npc->GetActorLocation();
    const auto FeedbackGenerationBeforeStop=Npc->GetTeamIntegrationComponent()->GetFeedbackGeneration();
    TestTrue(TEXT("high personal risk invokes real cancellation"),Suppression->ReassessSafety(0));
    TestFalse(TEXT("cancelled executor is inactive"),Suppression->IsActive());
    TestFalse(TEXT("cancelled executor no longer sprays"),Suppression->IsSpraying());
    const auto& Stopped=Npc->GetTeamIntegrationComponent()->GetInteractionOpportunities();
    TestFalse(TEXT("handoff clears holding snapshot"),Stopped.bHoldingExtinguisher);
    TestFalse(TEXT("handoff clears available fire attempt snapshot"),Stopped.bSuppressibleFireKnown);
    TestFalse(TEXT("handoff clears approach snapshot"),Stopped.bSuppressionApproachKnown);
    TestTrue(TEXT("handoff clears stale approach coordinate"),Stopped.SuppressionApproachLocation.IsZero());
    TestEqual(TEXT("return uses current JJW personality/social movement speed"),Npc->GetCharacterMovement()->MaxWalkSpeed,ExpectedJjwSpeed);
    TestTrue(TEXT("failed optional retreat preserves authoritative JJW evacuation commitment"),
        Npc->GetBehaviorStateMachine()->HasCommittedToEvacuation() && Npc->GetCurrentIntent()==EYUFSIntent::CommitEvac);
    TestFalse(TEXT("no stale approach continues after cancellation"),Npc->GetNavigator()->IsFollowingPath());
    TestFalse(TEXT("cancellation has no pending approach request"),Npc->GetNavigator()->bIsPathfinding);
    TestTrue(TEXT("cancellation does not teleport"),Npc->GetActorLocation().Equals(OriginalLocation));
    TestTrue(TEXT("cancellation publishes feedback before handing off to a newer request"),
        Npc->GetTeamIntegrationComponent()->GetFeedbackGeneration()>FeedbackGenerationBeforeStop);
    TestTrue(TEXT("handoff feedback is never a fake fire completion"),
        Npc->GetTeamIntegrationComponent()->GetInteractionFeedback().Status!=EYUFSTeamRequestStatus::Completed);
    TestEqual(TEXT("personal risk history survives without leaking old feedback into a new request"),Suppression->GetLastStopReason(),
        FName(TEXT("PerceivedRiskOrEvacuationPriority")));
    TestEqual(TEXT("personal risk cancellation does not change the FDS frame"),RecordedFire->GetFrame(),17);
    TestTrue(TEXT("personal risk cancellation does not extinguish recorded fire playback"),RecordedFire->IsPlaying());

    Suppression=ArmAttempt(Npc);
    Suppression->LatestObservation.NearbyHeatNormalized=.65f;
    Suppression->LatestObservation.NearbyHeat=.65f;
    Npc->GetBehaviorStateMachine()->TickStateMachine(.1f,Suppression->LatestObservation);
    TestTrue(TEXT("near heat emergency reaches the same real cancellation path"),Suppression->ReassessSafety(0));
    TestEqual(TEXT("near heat records immediate danger"),Suppression->GetLastStopReason(),
        FName(TEXT("ImmediateObservedDanger")));
    TestFalse(TEXT("near heat cancellation stops execution"),Suppression->IsActive());
    TestEqual(TEXT("near heat cancellation retains JJW personality speed"),Npc->GetCharacterMovement()->MaxWalkSpeed,Npc->GetDesiredWalkingSpeed());

    auto* Lifecycle=ArmAttempt(LifecycleNpc);
    const auto IntentBeforeStop=LifecycleNpc->GetCurrentIntent();
    const auto NavigationRevisionBeforeStop=LifecycleNpc->GetTeamIntegrationComponent()->GetNavigationDirective().Revision;
    Lifecycle->bRetreatReachable=true;
    Lifecycle->RetreatExit=FVector(800,0,0);
    Lifecycle->RetreatPath={FVector(500,0,0),FVector(800,0,0)};
    Lifecycle->Cancel(false);
    TestFalse(TEXT("lifecycle cancellation releases execution"),Lifecycle->IsActive());
    TestTrue(TEXT("lifecycle cancellation must not initiate evacuation or Shelter"),LifecycleNpc->GetCurrentIntent()==IntentBeforeStop);
    TestEqual(TEXT("lifecycle cancellation must not republish a movement request"),
        LifecycleNpc->GetTeamIntegrationComponent()->GetNavigationDirective().Revision,NavigationRevisionBeforeStop);
    TestFalse(TEXT("lifecycle cancellation does not restart navigation"),LifecycleNpc->GetNavigator()->bIsPathfinding);
    TestEqual(TEXT("lifecycle cancellation still restores current JJW movement settings"),LifecycleNpc->GetCharacterMovement()->MaxWalkSpeed,LifecycleNpc->GetDesiredWalkingSpeed());
    TestTrue(TEXT("tried-fire memory exists until an episode reset"),Lifecycle->FinishedFires.Contains(RecordedFire->GetFName()));
    Lifecycle->LastFireSeenAt=Lifecycle->LastToolSeenAt=Lifecycle->LastHazardSampleAt=10.f;
    Lifecycle->AttemptSeconds=4.f; Lifecycle->UseSeconds=2.f;
    Lifecycle->ResetForEpisode();
    TestTrue(TEXT("new episode clears tried-fire memory"),Lifecycle->FinishedFires.IsEmpty());
    TestTrue(TEXT("new episode clears historical stop reason"),Lifecycle->GetLastStopReason().IsNone());
    TestFalse(TEXT("new episode clears old fire actor"),Lifecycle->Fire.IsValid());
    TestFalse(TEXT("new episode clears old tool actor"),Lifecycle->Tool.IsValid());
    TestTrue(TEXT("new episode clears cached retreat path"),Lifecycle->RetreatPath.IsEmpty());
    TestTrue(TEXT("new episode clears cached retreat coordinate"),Lifecycle->RetreatExit.IsZero());
    TestFalse(TEXT("new episode has no unverified retained retreat"),Lifecycle->bRetreatReachable);
    TestFalse(TEXT("new episode has no stale attack point state"),Lifecycle->bHasAttackPoint);
    TestEqual(TEXT("new episode clears active attempt timer"),Lifecycle->AttemptSeconds,0.f);
    TestEqual(TEXT("new episode clears spray timer"),Lifecycle->UseSeconds,0.f);
    TestTrue(TEXT("new episode requires fresh physical evidence"),Lifecycle->LastHazardSampleAt<0.f
        && Lifecycle->LastFireSeenAt<0.f && Lifecycle->LastToolSeenAt<0.f);
    TestTrue(TEXT("episode reset does not start a new intent"),LifecycleNpc->GetCurrentIntent()==IntentBeforeStop);

    auto* Incapacitated=ArmAttempt(IncapacitatedNpc);
    IncapacitatedNpc->GetBehaviorStateMachine()->Config->SmokeExposureAccumRate=1.f;
    FYUFSNPCObservation DenseSmoke;
    DenseSmoke.SmokeDensityAtSelf=1.f;
    for (int32 Tick=0; Tick<20; ++Tick) IncapacitatedNpc->GetBehaviorStateMachine()->TickStateMachine(.1f,DenseSmoke);
    TestTrue(TEXT("incapacity interrupts a real active attempt"),Incapacitated->ReassessSafety(0));
    TestTrue(TEXT("handoff cannot revive an incapacitated NPC"),IncapacitatedNpc->GetBehaviorStateMachine()->IsIncapacitated());
    TestEqual(TEXT("incapacity overrides restored native speed with zero"),IncapacitatedNpc->GetCharacterMovement()->MaxWalkSpeed,0.f);
    TestFalse(TEXT("incapacitated NPC does not follow an approach path"),IncapacitatedNpc->GetNavigator()->IsFollowingPath());
    TestEqual(TEXT("all cancellations leave original fire frame unchanged"),RecordedFire->GetFrame(),17);
    TestTrue(TEXT("all cancellations leave recorded fire playing"),RecordedFire->IsPlaying());
    World->DestroyWorld(false);
    return true;
}

#endif
