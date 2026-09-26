#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Animation/AnimationAsset.h"
#include "Core/YUFSDeterministicRng.h"
#include "Core/YUFSObservation.h"
#include "Engine/SkeletalMesh.h"
#include "NPC/Decision/YUFSBeliefComponent.h"
#include "NPC/Decision/YUFSHumanBehaviorSelectorComponent.h"
#include "NPC/Decision/YUFSIntentComponent.h"
#include "NPC/Animation/YUFSActionAnimationComponent.h"
#include "NPC/Cognition/YUFSBehaviorPolicy.h"
#include "NPC/Cognition/YUFSHumanCognitionComponent.h"
#include "NPC/Integration/YUFSTeamIntegrationComponent.h"
#include "NPC/Tasks/YUFSActionTaskComponent.h"
#include "Fire/YUFSInteractionDoor.h"
#include "NPC/Integration/YUFSNpcEnvironmentInteraction.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FYUFSActionAnimationMappingTest,
	"YUFS.NPC.Animation.ActionMappingCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FYUFSActionAnimationMappingTest::RunTest(const FString& Parameters)
{
	const UYUFSActionAnimationComponent* Animations = NewObject<UYUFSActionAnimationComponent>();
	const USkeletalMesh* NPCMesh = LoadObject<USkeletalMesh>(
		nullptr,
		TEXT("/Game/NPCs/Crawling__1_.Crawling__1_"));
	TestNotNull(TEXT("NPC preview mesh is loadable"), NPCMesh);

	const int32 ActionCount = static_cast<int32>(EYUFSAction::Film) + 1;
	for (int32 ActionIndex = 0; ActionIndex < ActionCount; ++ActionIndex)
	{
		const EYUFSAction Action = static_cast<EYUFSAction>(ActionIndex);
		const FString ActionName = StaticEnum<EYUFSAction>()->GetNameStringByValue(ActionIndex);
		TestTrue(
			FString::Printf(TEXT("%s has an animation binding"), *ActionName),
			Animations->HasAnimationForAction(Action));
		TestFalse(
			FString::Printf(TEXT("%s has a non-empty asset path"), *ActionName),
			Animations->GetConfiguredAnimationPath(Action).IsEmpty());

		const FString AnimationPath = Animations->GetConfiguredAnimationPath(Action);
		const UAnimationAsset* Animation = LoadObject<UAnimationAsset>(nullptr, *AnimationPath);
		TestNotNull(
			FString::Printf(TEXT("%s animation asset is loadable"), *ActionName),
			Animation);
		if (NPCMesh && Animation)
		{
			TestTrue(
				FString::Printf(TEXT("%s animation uses the NPC skeleton"), *ActionName),
				Animation->GetSkeleton() == NPCMesh->GetSkeleton());
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FYUFSDeterministicRngTest,
	"YUFS.NPC.Decision.DeterministicRng",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FYUFSDeterministicRngTest::RunTest(const FString& Parameters)
{
	FYUFSDeterministicRngSet First;
	FYUFSDeterministicRngSet Second;
	First.Initialize(20260831, 17);
	Second.Initialize(20260831, 17);

	for (int32 Index = 0; Index < 16; ++Index)
	{
		TestEqual(
			FString::Printf(TEXT("same seed draw %d"), Index),
			First.FRand(EYUFSRngStream::Decision),
			Second.FRand(EYUFSRngStream::Decision));
	}

	FYUFSDeterministicRngSet WithExtraTaskDraw;
	FYUFSDeterministicRngSet WithoutExtraTaskDraw;
	WithExtraTaskDraw.Initialize(99, 3);
	WithoutExtraTaskDraw.Initialize(99, 3);
	WithExtraTaskDraw.FRand(EYUFSRngStream::TaskDuration);
	TestEqual(
		TEXT("task duration draws do not perturb route stream"),
		WithExtraTaskDraw.FRand(EYUFSRngStream::Route),
		WithoutExtraTaskDraw.FRand(EYUFSRngStream::Route));

	FYUFSDeterministicRngSet WithTaskChoiceDraw;
	FYUFSDeterministicRngSet WithoutTaskChoiceDraw;
	WithTaskChoiceDraw.Initialize(99, 3);
	WithoutTaskChoiceDraw.Initialize(99, 3);
	WithTaskChoiceDraw.FRand(EYUFSRngStream::TaskChoice);
	TestEqual(
		TEXT("task choice draws do not perturb decision stream"),
		WithTaskChoiceDraw.FRand(EYUFSRngStream::Decision),
		WithoutTaskChoiceDraw.FRand(EYUFSRngStream::Decision));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FYUFSBeliefOddsTest,
	"YUFS.NPC.Decision.BeliefOdds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FYUFSBeliefOddsTest::RunTest(const FString& Parameters)
{
	UYUFSBeliefComponent* Belief = NewObject<UYUFSBeliefComponent>();
	FYUFSNPCObservation Observation;

	Observation.bAlarmSounding = true;
	Belief->UpdateBelief(Observation);
	TestTrue(TEXT("alarm base is approximately 0.25"), FMath::IsNearlyEqual(Belief->GetCommitProbability(), 0.25f, 0.001f));

	Observation = FYUFSNPCObservation{};
	Observation.SmokeDensityAtSelf = 0.40f;
	Belief->UpdateBelief(Observation);
	TestTrue(TEXT("confirmed smoke base is approximately 0.65"), FMath::IsNearlyEqual(Belief->GetCommitProbability(), 0.65f, 0.001f));

	Observation.bReceivedStaffGuidance = true;
	Belief->UpdateBelief(Observation);
	TestTrue(TEXT("verified official instruction bypasses probability gate"), Belief->GetCommitProbability() >= 0.999f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FYUFSIntentDecisionPointTest,
	"YUFS.NPC.Decision.IntentDecisionPoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FYUFSIntentDecisionPointTest::RunTest(const FString& Parameters)
{
	UYUFSBeliefComponent* Belief = NewObject<UYUFSBeliefComponent>();
	UYUFSIntentComponent* Intent = NewObject<UYUFSIntentComponent>();
	FYUFSDeterministicRngSet RandomSource;
	RandomSource.Initialize(20260831, 42);

	FYUFSNPCObservation Observation;
	Belief->UpdateBelief(Observation);
	Intent->UpdateIntent(10.f, Observation, *Belief, true, RandomSource);
	TestEqual(TEXT("no emergency cue remains observe"), Intent->GetCurrentIntent(), EYUFSIntent::Observe);
	TestEqual(TEXT("no cue does not consume a decision draw"), RandomSource.GetDrawCount(EYUFSRngStream::Decision), 0ull);

	Observation.bAlarmSounding = true;
	Belief->UpdateBelief(Observation);
	Intent->UpdateIntent(1.f, Observation, *Belief, true, RandomSource);
	const uint64 InitialDecisionDraws = RandomSource.GetDrawCount(EYUFSRngStream::Decision);
	TestTrue(TEXT("first emergency cue performs an appraisal draw"), InitialDecisionDraws >= 1ull);
	if (Intent->GetCurrentIntent() != EYUFSIntent::CommitEvac)
	{
		TestEqual(
			TEXT("observed action-count bands remain calibration targets"),
			Intent->GetPreActionTargetCount(),
			0);
	}

	for (int32 Index = 0; Index < 20; ++Index)
	{
		Belief->UpdateBelief(Observation);
		Intent->UpdateIntent(1.f, Observation, *Belief, true, RandomSource);
	}
	TestEqual(TEXT("timer reassessment does not accumulate Bernoulli draws"), RandomSource.GetDrawCount(EYUFSRngStream::Decision), InitialDecisionDraws);

	UYUFSIntentComponent* HelperIntent = NewObject<UYUFSIntentComponent>();
	HelperIntent->bForceHelpIntentOnObservedNeed = true;
	Observation = FYUFSNPCObservation{};
	Observation.bNearbyNPCNeedsHelp = true;
	Belief->UpdateBelief(Observation);
	HelperIntent->UpdateIntent(1.f, Observation, *Belief, true, RandomSource);
	TestEqual(TEXT("nearby help request maps to help intent"), HelperIntent->GetCurrentIntent(), EYUFSIntent::Help);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FYUFSHumanCognitionEvidenceTest,
	"YUFS.NPC.Decision.HumanCognitionEvidenceRevision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FYUFSHumanCognitionEvidenceTest::RunTest(const FString& Parameters)
{
	UYUFSHumanCognitionComponent* Cognition = NewObject<UYUFSHumanCognitionComponent>();
	Cognition->bGenerateDeterministicTraitVariation = false;
	FYUFSNPCObservation Observation;

	Cognition->UpdateCognition(0.1f, Observation);
	const int64 InitialRevision = Cognition->GetCognitiveState().EvidenceRevision;
	TestEqual(TEXT("initial snapshot creates one evidence revision"), InitialRevision, 1ll);
	Cognition->UpdateCognition(0.1f, Observation);
	TestEqual(
		TEXT("unchanged snapshot does not create another revision"),
		Cognition->GetCognitiveState().EvidenceRevision,
		InitialRevision);

	Observation.bAlarmSounding = true;
	Cognition->UpdateCognition(0.1f, Observation);
	TestEqual(
		TEXT("alarm is an ambiguous physical cue"),
		Cognition->GetCognitiveState().PhysicalSeverity,
		EYUFSPerceivedPhysicalSeverity::AmbiguousAlarm);
	TestEqual(
		TEXT("new alarm increments evidence revision"),
		Cognition->GetCognitiveState().EvidenceRevision,
		InitialRevision + 1);

	Observation.SmokeDensityAtSelf = 0.40f;
	Observation.bHazardSampleAvailable = true;
	Cognition->UpdateCognition(0.1f, Observation);
	TestEqual(
		TEXT("smoke promotes the mutually exclusive severity tier"),
		Cognition->GetCognitiveState().PhysicalSeverity,
		EYUFSPerceivedPhysicalSeverity::ConfirmedSmoke);
	TestTrue(TEXT("smoke escalation latches a freeze consideration cue"), Cognition->ShouldConsiderFreeze());
	TestTrue(TEXT("freeze cue can be consumed once"), Cognition->ConsumeFreezeCue());
	TestFalse(TEXT("consumed freeze cue is cleared"), Cognition->ConsumeFreezeCue());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FYUFSHumanBehaviorSelectionTest,
	"YUFS.NPC.Decision.HumanBehaviorSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FYUFSHumanBehaviorSelectionTest::RunTest(const FString& Parameters)
{
	UYUFSBehaviorPolicy* Policy = NewObject<UYUFSBehaviorPolicy>();
	Policy->SeekInformationWeight = 0.f;
	Policy->WaitOrContinueWeight = 0.f;
	Policy->RetrieveBelongingsWeight = 1.f;
	Policy->WarnOrAssistWeight = 0.f;
	Policy->ObserveOrRecordWeight = 0.f;
	Policy->FreezeBaseProbability = 0.f;
	Policy->bUseLegacyBelongingsAssumption = false;

	UYUFSHumanBehaviorSelectorComponent* Selector = NewObject<UYUFSHumanBehaviorSelectorComponent>();
	Selector->PolicyAsset = Policy;
	FYUFSDeterministicRngSet RandomSource;
	RandomSource.Initialize(42, 7);
	FYUFSNPCObservation Observation;
	Observation.bAlarmSounding = true;
	Observation.CurrentState = EYUFSBehaviorState::Preparing;
	FYUFSCognitiveState CognitiveState;
	CognitiveState.PhysicalSeverity = EYUFSPerceivedPhysicalSeverity::AmbiguousAlarm;
	FYUFSHumanTraits Traits;
	FYUFSInteractionOpportunitySnapshot Opportunities;
	Opportunities.KnowledgeRevision = 1;
	Opportunities.bBelongingsKnown = true;
	Opportunities.BelongingsStableId = TEXT("BAG-01");

	const FYUFSBehaviorDecision First = Selector->ResolveDecision(
		EYUFSAction::GatherBelongings,
		EYUFSIntent::Prepare,
		Observation,
		CognitiveState,
		Traits,
		Opportunities,
		false,
		RandomSource);
	TestEqual(TEXT("known belongings can be selected while preparing"), First.Behavior, EYUFSHighLevelBehavior::RetrieveBelongings);
	TestEqual(TEXT("belongings use the explicit task layer"), First.DesiredTask, EYUFSActionTask::GatherBelongings);
	const uint64 DrawsAfterFirstSelection = RandomSource.GetDrawCount(EYUFSRngStream::TaskChoice);

	const FYUFSBehaviorDecision Second = Selector->ResolveDecision(
		EYUFSAction::GatherBelongings,
		EYUFSIntent::Prepare,
		Observation,
		CognitiveState,
		Traits,
		Opportunities,
		false,
		RandomSource);
	TestEqual(TEXT("stable appraisal keeps the same behavior revision"), Second.Revision, First.Revision);
	TestEqual(
		TEXT("stable appraisal does not redraw task choice"),
		RandomSource.GetDrawCount(EYUFSRngStream::TaskChoice),
		DrawsAfterFirstSelection);

	Observation.CurrentState = EYUFSBehaviorState::Evacuating;
	Selector->ResolveDecision(
		EYUFSAction::EvacuateToNearestExit,
		EYUFSIntent::CommitEvac,
		Observation,
		CognitiveState,
		Traits,
		Opportunities,
		false,
		RandomSource);
	TestEqual(TEXT("commitment projects to evacuation, never an extinguishing attempt"),
		Selector->GetCurrentDecision().Behavior, EYUFSHighLevelBehavior::EvacuateNearest);
	TestEqual(TEXT("evacuation projection does not gamble"),
		RandomSource.GetDrawCount(EYUFSRngStream::TaskChoice), DrawsAfterFirstSelection);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FYUFSTeamIntegrationContractTest,
	"YUFS.NPC.Integration.TeamContracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FYUFSTeamIntegrationContractTest::RunTest(const FString& Parameters)
{
	UYUFSTeamIntegrationComponent* Integration = NewObject<UYUFSTeamIntegrationComponent>();
	FYUFSInteractionOpportunitySnapshot Opportunities;
	Opportunities.KnowledgeRevision = 1;
	Opportunities.bBelongingsKnown = true;
	Opportunities.BelongingsStableId = TEXT("BAG-01");
	Opportunities.BelongingsLocation = FVector(100.f, 200.f, 0.f);
	Integration->SubmitInteractionOpportunities(Opportunities);

	FYUFSBehaviorDecision Decision;
	Decision.Behavior = EYUFSHighLevelBehavior::RetrieveBelongings;
	Decision.LegacyAction = EYUFSAction::GatherBelongings;
	Decision.DesiredTask = EYUFSActionTask::GatherBelongings;
	Decision.Reason = TEXT("TestBelongings");
	FYUFSCognitiveState Cognition;
	Integration->PublishDecision(17, Decision, EYUFSIntent::Prepare, EYUFSBehaviorState::Preparing,
		FVector::ZeroVector, true, Cognition);
	TestEqual(TEXT("team route cannot bypass JJW preparation"), Integration->GetNavigationDirective().Goal, EYUFSNavigationGoal::None);
	TestEqual(TEXT("team interaction cannot bypass JJW preparation"), Integration->GetInteractionDirective().Goal, EYUFSInteractionGoal::None);
	Integration->PublishDecision(
		17,
		Decision,
		EYUFSIntent::CommitEvac,
		EYUFSBehaviorState::Evacuating,
		FVector::ZeroVector,
		true,
		Cognition);

	TestEqual(
		TEXT("interaction team receives belongings retrieval"),
		Integration->GetInteractionDirective().Goal,
		EYUFSInteractionGoal::RetrieveBelongings);
	TestEqual(
		TEXT("interaction targets the left-behind bag"),
		Integration->GetInteractionDirective().TargetStableId,
		FName(TEXT("BAG-01")));
	TestEqual(
		TEXT("motion team receives gather-belongings semantic"),
		Integration->GetMotionDirective().Semantic,
		EYUFSMotionSemantic::GatherBelongings);
	TestEqual(
		TEXT("route team receives interaction destination"),
		Integration->GetNavigationDirective().Goal,
		EYUFSNavigationGoal::InteractionTarget);
	TestTrue(TEXT("route goes back to the bag"),
		Integration->GetNavigationDirective().DestinationHint.Equals(Opportunities.BelongingsLocation));
	const int64 NavigationRevision = Integration->GetNavigationDirective().Revision;
	Integration->PublishDecision(
		17,
		Decision,
		EYUFSIntent::CommitEvac,
		EYUFSBehaviorState::Evacuating,
		FVector::ZeroVector,
		true,
		Cognition);
	TestEqual(
		TEXT("unchanged directive does not create a new route revision"),
		Integration->GetNavigationDirective().Revision,
		NavigationRevision);

	FYUFSTeamRequestFeedback StaleFeedback;
	StaleFeedback.RequestRevision = NavigationRevision - 1;
	StaleFeedback.Status = EYUFSTeamRequestStatus::Blocked;
	Integration->SubmitNavigationFeedback(StaleFeedback);
	TestEqual(TEXT("stale route feedback is ignored"), Integration->GetFeedbackGeneration(), 0ll);

	FYUFSTeamRequestFeedback CurrentFeedback;
	CurrentFeedback.RequestRevision = NavigationRevision;
	CurrentFeedback.Status = EYUFSTeamRequestStatus::Accepted;
	Integration->SubmitNavigationFeedback(CurrentFeedback);
	TestEqual(TEXT("current route feedback is accepted"), Integration->GetFeedbackGeneration(), 1ll);

	// Bag picked up: the NPC evacuates again toward an exit.
	Opportunities.bBelongingsKnown = false;
	Opportunities.BelongingsStableId = NAME_None;
	Opportunities.BelongingsLocation = FVector::ZeroVector;
	Opportunities.bCarryingBelongings = true;
	++Opportunities.KnowledgeRevision;
	Integration->SubmitInteractionOpportunities(Opportunities);
	FYUFSBehaviorDecision Evacuate;
	Evacuate.Behavior = EYUFSHighLevelBehavior::EvacuateNearest;
	Evacuate.LegacyAction = EYUFSAction::EvacuateToNearestExit;
	const FVector Exit(500.f, 0.f, 0.f);
	Integration->PublishDecision(17, Evacuate, EYUFSIntent::CommitEvac, EYUFSBehaviorState::Evacuating,
		Exit, true, Cognition);
	TestEqual(TEXT("carrying NPC is routed to a safe exit"),
		Integration->GetNavigationDirective().Goal, EYUFSNavigationGoal::SafeExit);
	TestTrue(TEXT("exit destination is forwarded"), Integration->GetNavigationDirective().DestinationHint.Equals(Exit));
	TestEqual(TEXT("no interaction remains after pickup"),
		Integration->GetInteractionDirective().Goal, EYUFSInteractionGoal::None);
	TestEqual(TEXT("new navigation directive clears earlier feedback"),
		Integration->GetNavigationFeedback().Status, EYUFSTeamRequestStatus::None);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FYUFSActionTaskLifecycleTest,
	"YUFS.NPC.Decision.ActionTaskLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FYUFSActionTaskLifecycleTest::RunTest(const FString& Parameters)
{
	UYUFSActionTaskComponent* Tasks = NewObject<UYUFSActionTaskComponent>();
	Tasks->WaitObserveDuration = { 0.1f, 0.f, 0.1f };
	FYUFSDeterministicRngSet RandomSource;
	RandomSource.Initialize(7, 11);

	Tasks->UpdateTask(0.f, EYUFSAction::WaitForInfo, EYUFSIntent::Observe, false, false, RandomSource);
	EYUFSActionTask From = EYUFSActionTask::None;
	EYUFSActionTask To = EYUFSActionTask::None;
	EYUFSTaskCancelReason Reason = EYUFSTaskCancelReason::None;
	TestTrue(TEXT("task start event is emitted"), Tasks->ConsumeTaskEvent(From, To, Reason));
	TestEqual(TEXT("wait action starts wait task"), To, EYUFSActionTask::WaitForOfficialInfo);

	Tasks->UpdateTask(0.2f, EYUFSAction::WaitForInfo, EYUFSIntent::Observe, false, false, RandomSource);
	TestTrue(TEXT("task completion event is emitted"), Tasks->ConsumeTaskEvent(From, To, Reason));
	TestEqual(TEXT("completion reason is preserved"), Reason, EYUFSTaskCancelReason::Completed);
	TestEqual(TEXT("completed task ends"), Tasks->GetCurrentTask(), EYUFSActionTask::None);

	Tasks->UpdateTask(1.f, EYUFSAction::WaitForInfo, EYUFSIntent::Observe, false, false, RandomSource);
	TestFalse(TEXT("same completed action does not restart every tick"), Tasks->ConsumeTaskEvent(From, To, Reason));

	Tasks->UpdateTask(0.f, EYUFSAction::SeekInformation, EYUFSIntent::Observe, false, false, RandomSource);
	TestTrue(TEXT("action change starts a new task"), Tasks->ConsumeTaskEvent(From, To, Reason));
	TestEqual(TEXT("seek action starts seek task"), To, EYUFSActionTask::SeekInformation);

	Tasks->UpdateTask(0.f, EYUFSAction::WaitForInfo, EYUFSIntent::Observe, false, false, RandomSource);
	TestTrue(TEXT("task replacement retains cancellation event"), Tasks->ConsumeTaskEvent(From, To, Reason));
	TestEqual(TEXT("replacement cancellation reason"), Reason, EYUFSTaskCancelReason::IntentChanged);
	TestTrue(TEXT("task replacement also retains start event"), Tasks->ConsumeTaskEvent(From, To, Reason));
	TestEqual(TEXT("replacement target task"), To, EYUFSActionTask::WaitForOfficialInfo);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSInteractionResumeContractTest,
	"YUFS.NPC.Integration.InteractionResumeContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FYUFSInteractionResumeContractTest::RunTest(const FString& Parameters)
{
	auto* Intent = NewObject<UYUFSIntentComponent>();
	Intent->ResumeEvacuationAfterInteraction(true);
	TestEqual(TEXT("completed interaction resumes evacuation"), Intent->GetCurrentIntent(), EYUFSIntent::CommitEvac);
	Intent->ResumeEvacuationAfterInteraction(false);
	TestEqual(TEXT("no route means shelter, not invented escape"), Intent->GetCurrentIntent(), EYUFSIntent::Shelter);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSDoorLifecycleTest, "YUFS.NPC.Interaction.DoorLifecycle",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)
bool FYUFSDoorLifecycleTest::RunTest(const FString& Parameters)
{
 const auto Settings=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
 UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Settings);
 auto* Door=World->SpawnActor<AYUFSInteractionDoor>();
 auto* A=World->SpawnActor<AYUFSEvacuationNPC>(FVector(50,0,90),FRotator::ZeroRotator);
 FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 auto* B=World->SpawnActor<AYUFSEvacuationNPC>(FVector(-50,0,90),FRotator::ZeroRotator,Spawn);
 if (!TestNotNull(TEXT("door spawned"),Door) || !TestNotNull(TEXT("user A spawned"),A) || !TestNotNull(TEXT("user B spawned"),B)) { World->DestroyWorld(false); return false; }
 Door->bLocked=true; TestFalse(TEXT("locked door rejected"),Door->TryUse(A));
 Door->bLocked=false; Door->bHot=true; TestFalse(TEXT("hot door rejected"),Door->TryUse(A));
 Door->bHot=false;
 TestTrue(TEXT("first operator reserves"),Door->TryUse(A));
 TestFalse(TEXT("second operator cannot steal"),Door->TryUse(B));
 Door->Release(B); TestFalse(TEXT("non-owner cannot release"),Door->TryUse(B));
 Door->Release(A); TestTrue(TEXT("owner release enables next operator"),Door->TryUse(B));
 Door->Tick(.5f); TestFalse(TEXT("partial rotation blocks passage"),Door->IsOpen());
 Door->Tick(5.f); TestFalse(TEXT("waiting person in swing arc prevents forced opening"),Door->IsOpen());
 // Move the fixture's obstacle away; production interaction never teleports an NPC.
 A->SetActorLocation(FVector(240,-120,90));
 Door->Tick(5.f); TestTrue(TEXT("opening completes"),Door->IsOpen());
 TestTrue(TEXT("hinge actor remains in authored orientation"),FMath::IsNearlyZero(Door->GetActorRotation().Yaw,.01f));
 TestTrue(TEXT("leaf rotation cannot overshoot"),FMath::IsNearlyEqual(FMath::Abs(Door->Panel->GetComponentRotation().Yaw),90.f,.01f));
 TestEqual(TEXT("open solid leaf retains collision"),Door->Panel->GetCollisionEnabled(),ECollisionEnabled::QueryAndPhysics);
 TestTrue(TEXT("only completed doorway passage is unblocked"),Door->IsPassageClear());
 auto* Help=A->EnvironmentInteraction.Get(); Help->RequestAssistance(true);
 TestFalse(TEXT("cannot assist self"),Help->TryReserveHelper(A));
 TestTrue(TEXT("helper reserves person"),Help->TryReserveHelper(B));
 TestFalse(TEXT("reserved person not advertised"),Help->CanReceiveAssistance());
 Help->ReleaseHelper(A); TestFalse(TEXT("non-owner cannot release helper"),Help->CanReceiveAssistance());
 Help->ReleaseHelper(B); TestTrue(TEXT("released person available"),Help->CanReceiveAssistance());
 Help->bAcceptsAssistance=false; TestFalse(TEXT("refusal respected"),Help->TryReserveHelper(B));
 World->DestroyWorld(false); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSAssistContractTest, "YUFS.NPC.Interaction.AssistDirectiveTarget",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)
bool FYUFSAssistContractTest::RunTest(const FString& Parameters)
{
 auto* Team=NewObject<UYUFSTeamIntegrationComponent>();
 FYUFSInteractionOpportunitySnapshot Opportunity; Opportunity.KnowledgeRevision=1;
 Opportunity.bAssistPersonKnown=true; Opportunity.AssistPersonStableId=TEXT("Person7"); Opportunity.AssistPersonLocation=FVector(100,200,300);
 Team->SubmitInteractionOpportunities(Opportunity);
 FYUFSBehaviorDecision Decision; Decision.Behavior=EYUFSHighLevelBehavior::AssistOther;
 Team->PublishDecision(3,Decision,EYUFSIntent::Help,EYUFSBehaviorState::Normal,FVector::ZeroVector,true,FYUFSCognitiveState());
 TestEqual(TEXT("interaction targets discovered person"),Team->GetInteractionDirective().TargetStableId,Opportunity.AssistPersonStableId);
 TestEqual(TEXT("route targets same person"),Team->GetNavigationDirective().TargetStableId,Opportunity.AssistPersonStableId);
 TestTrue(TEXT("route receives perceived location"),Team->GetNavigationDirective().DestinationHint.Equals(Opportunity.AssistPersonLocation));
 Opportunity.bDoorActionRequired=true; Opportunity.DoorStableId=TEXT("Door1"); ++Opportunity.KnowledgeRevision; Team->SubmitInteractionOpportunities(Opportunity);
 Team->PublishDecision(3,Decision,EYUFSIntent::Help,EYUFSBehaviorState::Normal,FVector::ZeroVector,true,FYUFSCognitiveState());
 TestTrue(TEXT("door is a prerequisite interaction"),Team->GetInteractionDirective().Goal==EYUFSInteractionGoal::OpenDoor);
 TestTrue(TEXT("motion receives door operation"),Team->GetMotionDirective().Semantic==EYUFSMotionSemantic::OperateDoor);
 return true;
}

#endif
