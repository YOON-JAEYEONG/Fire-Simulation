#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/YUFSObservation.h"
#include "NPC/Cognition/YUFSHumanCognitionComponent.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "NPC/Behavior/YUFSBehaviorConfig.h"
#include "NPC/Perception/YUFSNPCPerceptionComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSUnknownHazardCognitionMemoryTest,
	"YUFS.NPC.UnknownHazard.CognitionRetainsThreatUntilValidClearSample",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FYUFSUnknownHazardCognitionMemoryTest::RunTest(const FString& Parameters)
{
	auto* Cognition = NewObject<UYUFSHumanCognitionComponent>();
	FYUFSNPCObservation Observation;
	Cognition->UpdateCognition(10.f, Observation);
	TestEqual(TEXT("initial missing data does not invent physical severity"), Cognition->GetCognitiveState().PhysicalSeverity, EYUFSPerceivedPhysicalSeverity::None);
	TestEqual(TEXT("initial missing data does not invent perceived danger"), Cognition->GetCognitiveState().PerceivedRisk, 0.f);

	// Values from an unavailable channel cannot create a fresh physical observation.
	Observation.SmokeDensityAtSelf = Observation.TemperatureAtSelf = Observation.NearbyHeatNormalized = 1.f;
	Cognition->UpdateCognition(10.f, Observation);
	TestEqual(TEXT("unavailable values do not manufacture a life threat"), Cognition->GetCognitiveState().PhysicalSeverity, EYUFSPerceivedPhysicalSeverity::None);
	TestEqual(TEXT("unavailable values do not manufacture perceived risk"), Cognition->GetCognitiveState().PerceivedRisk, 0.f);

	Observation.bHazardSampleAvailable = true;
	Observation.SmokeDensityAtSelf = .75f;
	Observation.TemperatureAtSelf = .70f;
	Observation.NearbyHeatNormalized = .70f;
	Cognition->UpdateCognition(2.f, Observation);
	const float RememberedRisk = Cognition->GetCognitiveState().PerceivedRisk;
	TestTrue(TEXT("valid threat creates high perceived risk"), RememberedRisk > .80f);
	TestEqual(TEXT("valid near heat records life threat"), Cognition->GetCognitiveState().PhysicalSeverity, EYUFSPerceivedPhysicalSeverity::ImmediateLifeThreat);

	Observation = FYUFSNPCObservation{};
	Observation.NearbyNPCCount = 6;
	Observation.NearbyEvacuatingRatio = 0.f;
	Cognition->UpdateCognition(20.f, Observation);
	TestEqual(TEXT("data outage cannot clear remembered physical severity"), Cognition->GetCognitiveState().PhysicalSeverity, EYUFSPerceivedPhysicalSeverity::ImmediateLifeThreat);
	TestTrue(TEXT("stationary crowd cannot lower perceived risk while physical data is unknown"), Cognition->GetCognitiveState().PerceivedRisk >= RememberedRisk);

	Observation = FYUFSNPCObservation{};
	Observation.bHazardSampleAvailable = true; // A real measured clear frame, unlike the outage above.
	Cognition->UpdateCognition(20.f, Observation);
	TestEqual(TEXT("fresh valid clear sample can clear the physical cue"), Cognition->GetCognitiveState().PhysicalSeverity, EYUFSPerceivedPhysicalSeverity::None);
	TestTrue(TEXT("risk may decrease again after a valid clear sample"), Cognition->GetCognitiveState().PerceivedRisk < RememberedRisk);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSUnknownHazardCommunicationTest,
	"YUFS.NPC.UnknownHazard.AlarmAndOfficialEvidenceStillRaiseUrgency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FYUFSUnknownHazardCommunicationTest::RunTest(const FString& Parameters)
{
	auto* Cognition = NewObject<UYUFSHumanCognitionComponent>();
	Cognition->Traits.AuthorityTrust = 1.f;
	FYUFSNPCObservation Observation;
	Observation.bAlarmSounding = true;
	Cognition->UpdateCognition(2.f, Observation);
	const float AlarmRisk = Cognition->GetCognitiveState().PerceivedRisk;
	const float AlarmUrgency = Cognition->GetCognitiveState().Urgency;
	TestTrue(TEXT("alarm remains meaningful without physical samples"), AlarmRisk > 0.f);
	TestEqual(TEXT("alarm alone does not claim observed smoke"), Cognition->GetCognitiveState().PhysicalSeverity, EYUFSPerceivedPhysicalSeverity::AmbiguousAlarm);
	Observation.bReceivedLiveAnnouncement = true;
	Observation.bReceivedStaffGuidance = true;
	Observation.NearbyNPCCount = 5;
	Observation.NearbyEvacuatingRatio = .8f;
	Cognition->UpdateCognition(2.f, Observation);
	TestTrue(TEXT("official/social evidence may raise risk during an outage"), Cognition->GetCognitiveState().PerceivedRisk > AlarmRisk);
	TestTrue(TEXT("official/social evidence may raise urgency during an outage"), Cognition->GetCognitiveState().Urgency > AlarmUrgency);
	TestEqual(TEXT("official instruction does not fabricate a physical smoke sample"), Cognition->GetCognitiveState().PhysicalSeverity, EYUFSPerceivedPhysicalSeverity::AmbiguousAlarm);
	return true;
}

// The authoritative JJW PADM no longer accepts our old ApplyCognitiveRisk
// mutation or promises the retired legacy risk floor/exposure recovery policy.
// Check missing-data provenance at the shared perception boundary instead.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSUnknownHazardPerceptionStatusTest,
    "YUFS.NPC.UnknownHazard.JJWPerceptionDistinguishesUnavailableAndClear",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSUnknownHazardPerceptionStatusTest::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    ACharacter* Actor = World->SpawnActor<ACharacter>();
    Actor->SetActorLocation(FVector(100, 100, 100));
    auto* Perception = NewObject<UYUFSNPCPerceptionComponent>(Actor);
    Perception->RegisterComponent();
    Perception->Config = NewObject<UYUFSPerceptionConfig>(Perception);
    Perception->Config->VisionRayCount = 1;
    Perception->Config->VisionRange = 500.f;

    Perception->UpdateFromSnapshot(FYUFSHazardSnapshot(), 0.f);
    TestEqual(TEXT("Absent data remains explicitly MissingData"), Perception->GetDataStatus(), EYUFSHazardDataStatus::MissingData);
    TestEqual(TEXT("Missing data does not invent a fresh local reading"), Perception->GetTemperature(), 0.f);

    auto HotGrid = MakeShared<FYUFSHazardGrid, ESPMode::ThreadSafe>();
    HotGrid->Dimensions = FIntVector(20, 10, 10);
    HotGrid->Density.Init(128, 2000);
    HotGrid->Temperature.Init(255, 2000);
    FYUFSHazardSnapshot Hot;
    Hot.Grid = HotGrid;
    Hot.GridToWorld = FTransform(FQuat::Identity, FVector::ZeroVector, FVector(40));
    Hot.Status = EYUFSHazardDataStatus::Ready;
    Hot.Frame = 1;
    Perception->UpdateFromSnapshot(Hot, 1.f);
    const auto InFlight = Perception->RestrictToKnowledge(Hot);
    const int32 KnownCount = Perception->GetKnownCellCount();
    TestTrue(TEXT("Measured heat becomes personal knowledge"), KnownCount > 0);
    TestTrue(TEXT("Measured heat is available to the current observation"), Perception->GetNearbyHeat() > .5f);

    FYUFSHazardSnapshot Loading = Hot;
    Loading.Status = EYUFSHazardDataStatus::Loading;
    Perception->UpdateFromSnapshot(Loading, 2.f);
    TestEqual(TEXT("Streaming gap is not a measured clear frame"), Perception->GetDataStatus(), EYUFSHazardDataStatus::Loading);
    TestEqual(TEXT("Unavailable readings are not copied into the next observation"), Perception->GetNearbyHeat(), 0.f);
    TestEqual(TEXT("Short data gap preserves bounded personal memory"), Perception->GetKnownCellCount(), KnownCount);

    auto ClearGrid = MakeShared<FYUFSHazardGrid, ESPMode::ThreadSafe>();
    ClearGrid->Dimensions = HotGrid->Dimensions;
    ClearGrid->Density.Init(0, 2000);
    ClearGrid->Temperature.Init(0, 2000);
    FYUFSHazardSnapshot Clear = Hot;
    Clear.Grid = ClearGrid;
    Clear.Frame = 2;
    Perception->UpdateFromSnapshot(Clear, 3.f);
    TestEqual(TEXT("Actual clear data has Ready status"), Perception->GetDataStatus(), EYUFSHazardDataStatus::Ready);
    TestEqual(TEXT("Reobserved clear cells erase obsolete hazards"), Perception->GetKnownCellCount(), 0);
    TestTrue(TEXT("An in-flight immutable snapshot retains its old observation"), InFlight.KnownCells.IsValid() && InFlight.KnownCells->Num() > 0);
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSUnknownHazardJJWCommunicationTest,
    "YUFS.NPC.UnknownHazard.JJWCommunicationNeedsObservation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSUnknownHazardJJWCommunicationTest::RunTest(const FString&)
{
    auto* State = NewObject<UYUFSBehaviorStateMachine>();
    State->Config = NewObject<UYUFSBehaviorConfig>(State);
    State->InitializePersonality(40);
    FYUFSNPCObservation Observation;
    Observation.bAlarmSounding = true;
    State->TickStateMachine(2.f, Observation);
    const float AlarmRisk = State->GetRiskPerception();
    TestTrue(TEXT("Alarm contributes without manufacturing physical samples"), AlarmRisk > 0.f);
    TestFalse(TEXT("Hearing an alarm is not eyewitness evidence"), State->HasRecentDirectEvidence());

    State->OnStaffGuidanceReceived();
    TestEqual(TEXT("Legacy event callback cannot directly overwrite JJW risk"), State->GetRiskPerception(), AlarmRisk);
    Observation.bReceivedStaffGuidance = true;
    State->TickStateMachine(1.f, Observation);
    TestTrue(TEXT("Actual observed guidance raises evidence-based risk"), State->GetRiskPerception() > AlarmRisk);
    TestFalse(TEXT("Guidance never masquerades as own physical observation"), State->HasRecentDirectEvidence());
    TestEqual(TEXT("Guidance retains its correct cause"), State->GetDecisionCue(), EYUFSEvacuationCue::Guidance);
    return true;
}
#endif
