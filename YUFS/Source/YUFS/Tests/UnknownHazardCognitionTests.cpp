#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Core/YUFSObservation.h"
#include "NPC/Cognition/YUFSHumanCognitionComponent.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "NPC/Behavior/YUFSBehaviorConfig.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSUnknownHazardExposureTest,
	"YUFS.NPC.UnknownHazard.NoInventedExposureOrRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FYUFSUnknownHazardExposureTest::RunTest(const FString& Parameters)
{
	auto* State = NewObject<UYUFSBehaviorStateMachine>();
	State->Config = NewObject<UYUFSBehaviorConfig>();
	State->Config->SmokeExposureAccumRate = .1f;
	State->Config->CrawlThreshold = .4f;
	State->Config->IncapacitationThreshold = .9f;
	FYUFSNPCObservation Observation;
	State->TickStateMachine(20.f, Observation);
	TestEqual(TEXT("initial unknown creates no exposure"), State->GetSmokeExposure(), 0.f);
	TestEqual(TEXT("initial unknown does not create emergency behavior"), State->GetCurrentState(), EYUFSBehaviorState::Normal);
	Observation.bHazardSampleAvailable = true;
	Observation.SmokeDensityAtSelf = .5f;
	State->TickStateMachine(10.f, Observation);
	const float Exposure = State->GetSmokeExposure();
	TestTrue(TEXT("valid smoke accumulates a test dose"), FMath::IsNearlyEqual(Exposure, .5f));
	TestTrue(TEXT("valid dose enters crawling"), State->IsCrawling());
	Observation = FYUFSNPCObservation{};
	State->TickStateMachine(30.f, Observation);
	TestEqual(TEXT("unknown zero is not fresh-air recovery"), State->GetSmokeExposure(), Exposure);
	TestTrue(TEXT("unknown data does not restore walking"), State->IsCrawling());
	Observation.SmokeDensityAtSelf = 1.f;
	State->TickStateMachine(30.f, Observation);
	TestEqual(TEXT("unavailable stale positive value does not invent additional injury"), State->GetSmokeExposure(), Exposure);
	TestFalse(TEXT("missing data cannot fabricate incapacitation"), State->IsIncapacitated());
	Observation = FYUFSNPCObservation{};
	Observation.bHazardSampleAvailable = true;
	State->TickStateMachine(20.f, Observation);
	TestTrue(TEXT("actual clear data permits configured gradual recovery"), State->GetSmokeExposure() < Exposure);
	TestFalse(TEXT("walking can resume once actual recovery crosses the threshold"), State->IsCrawling());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSUnknownHazardLegacyRiskTest,
	"YUFS.NPC.UnknownHazard.LegacyRiskFloorAndSocialUpdates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FYUFSUnknownHazardLegacyRiskTest::RunTest(const FString& Parameters)
{
	auto* State = NewObject<UYUFSBehaviorStateMachine>();
	State->Config = NewObject<UYUFSBehaviorConfig>();
	State->ApplyCognitiveRisk(.25f);
	FYUFSNPCObservation Observation;
	Observation.NearbyNPCCount = 6;
	State->TickStateMachine(20.f, Observation);
	TestEqual(TEXT("quiet crowd cannot erase known risk during an outage"), State->GetRiskPerception(), .25f);
	Observation.bAlarmSounding = true;
	State->TickStateMachine(2.f, Observation);
	TestTrue(TEXT("alarm still raises the legacy risk projection"), State->GetRiskPerception() > .25f);
	const float AlarmRisk = State->GetRiskPerception();
	State->OnStaffGuidanceReceived();
	TestTrue(TEXT("staff communication still raises risk"), State->GetRiskPerception() > AlarmRisk);
	const float GuidedRisk = State->GetRiskPerception();
	Observation = FYUFSNPCObservation{};
	Observation.bHazardSampleAvailable = true;
	State->TickStateMachine(2.f, Observation);
	TestTrue(TEXT("a measured clear interval permits legacy risk decay"), State->GetRiskPerception() < GuidedRisk);
	return true;
}

#endif
