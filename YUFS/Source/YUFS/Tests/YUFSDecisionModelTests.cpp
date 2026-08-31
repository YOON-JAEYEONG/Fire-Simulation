#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Animation/AnimationAsset.h"
#include "Core/YUFSDeterministicRng.h"
#include "Core/YUFSObservation.h"
#include "Engine/SkeletalMesh.h"
#include "NPC/Decision/YUFSBeliefComponent.h"
#include "NPC/Decision/YUFSIntentComponent.h"
#include "NPC/Animation/YUFSActionAnimationComponent.h"
#include "NPC/Tasks/YUFSActionTaskComponent.h"

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
		TestTrue(TEXT("failed appraisal samples a pre-action target"), Intent->GetPreActionTargetCount() >= 1);
		TestTrue(TEXT("pre-action target respects hard cap"), Intent->GetPreActionTargetCount() <= 15);
	}

	for (int32 Index = 0; Index < 20; ++Index)
	{
		Belief->UpdateBelief(Observation);
		Intent->UpdateIntent(1.f, Observation, *Belief, true, RandomSource);
	}
	TestEqual(TEXT("timer reassessment does not accumulate Bernoulli draws"), RandomSource.GetDrawCount(EYUFSRngStream::Decision), InitialDecisionDraws);

	UYUFSIntentComponent* HelperIntent = NewObject<UYUFSIntentComponent>();
	Observation = FYUFSNPCObservation{};
	Observation.bNearbyNPCNeedsHelp = true;
	Belief->UpdateBelief(Observation);
	HelperIntent->UpdateIntent(1.f, Observation, *Belief, true, RandomSource);
	TestEqual(TEXT("nearby help request maps to help intent"), HelperIntent->GetCurrentIntent(), EYUFSIntent::Help);

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

#endif
