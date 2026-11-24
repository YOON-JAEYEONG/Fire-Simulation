#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Communication/YUFSEmergencyCommSystem.h"
#include "Engine/World.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "NPC/YUFSEvacuationNPC.h"

struct FYUFSAlarmAwarenessTestAccess
{
	static void Observe(AYUFSEvacuationNPC& NPC, float Seconds)
	{
		NPC.RefreshAlarmAudibility();
		FYUFSNPCObservation Observation;
		NPC.BuildObservation(Observation);
		NPC.GetBehaviorStateMachine()->TickStateMachine(Seconds, Observation);
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSFireAwarenessSustainedAlarm, "YUFS.NPC.Awareness.SustainedAlarmBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSFireAwarenessSustainedAlarm::RunTest(const FString&)
{
	const auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Settings);
	if (!TestNotNull(TEXT("Game world"), World)) return false;
	// AActor::ProcessEvent requires an initialized world, even with BeginPlay dispatched.
	World->InitializeActorsForPlay(FURL());
	auto* Source = World->SpawnActor<AYUFSEmergencyCommSystem>();
	auto* NPC = World->SpawnActor<AYUFSEvacuationNPC>(FVector(1200, 0, 100), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("Alarm source"), Source) || !TestNotNull(TEXT("Listener"), NPC))
	{
		World->DestroyWorld(false);
		return false;
	}
	Source->AlarmRadius = 1000.f;
	auto* Mind = NPC->GetBehaviorStateMachine();
	Mind->Config = NewObject<UYUFSBehaviorConfig>(NPC);
	// Install the subscription through the same BeginPlay path as PIE.
	NPC->DispatchBeginPlay();

	Source->ActivateAlarm();
	FYUFSAlarmAwarenessTestAccess::Observe(*NPC, 0.1f);
	TestFalse(TEXT("Distant resident does not hear the alarm"), NPC->IsAlarmSounding());
	TestEqual(TEXT("No hazard information leaks to resident outside coverage"), Mind->GetCurrentState(), EYUFSBehaviorState::Normal);

	NPC->SetActorLocation(FVector(900, 0, 100));
	FYUFSAlarmAwarenessTestAccess::Observe(*NPC, 0.1f);
	TestTrue(TEXT("Entering coverage after the one-shot start signal receives the sustained alarm"), NPC->IsAlarmSounding());
	TestEqual(TEXT("Receipt supplies a cue without forcing evacuation"), Mind->GetCurrentState(), EYUFSBehaviorState::Perceiving);
	TestFalse(TEXT("Alarm perception does not bypass individual decision time"), Mind->HasCommittedToEvacuation());

	NPC->SetActorLocation(FVector(0, 0, 1050));
	FYUFSAlarmAwarenessTestAccess::Observe(*NPC, 0.1f);
	TestFalse(TEXT("Listener beyond the vertical radius no longer hears the alarm"), NPC->IsAlarmSounding());

	NPC->SetActorLocation(FVector(900, 0, 100));
	FYUFSAlarmAwarenessTestAccess::Observe(*NPC, 0.1f);
	TestTrue(TEXT("Re-entering while it is active hears it again"), NPC->IsAlarmSounding());
	Source->DeactivateAlarm();
	FYUFSAlarmAwarenessTestAccess::Observe(*NPC, 0.1f);
	TestFalse(TEXT("Deactivation does not leave an audible flag stuck true"), NPC->IsAlarmSounding());
	Source->ActivateAlarm();
	TestTrue(TEXT("A resident already in range receives the activation event"), NPC->IsAlarmSounding());
	Source->DeactivateAlarm();
	FYUFSAlarmAwarenessTestAccess::Observe(*NPC, 0.1f);
	TestFalse(TEXT("An alarm stopped before the next observation is not latched"), NPC->IsAlarmSounding());

	auto* Late = World->SpawnActor<AYUFSEvacuationNPC>(FVector(0, 650, 100), FRotator::ZeroRotator);
	if (TestNotNull(TEXT("Late listener"), Late))
	{
		Late->GetBehaviorStateMachine()->Config = NewObject<UYUFSBehaviorConfig>(Late);
		Source->ActivateAlarm();
		Late->DispatchBeginPlay();
		FYUFSAlarmAwarenessTestAccess::Observe(*Late, 0.1f);
		TestTrue(TEXT("Listener registered after activation hears the currently active alarm"), Late->IsAlarmSounding());
	}
	World->DestroyWorld(false);
	return true;
}
#endif
