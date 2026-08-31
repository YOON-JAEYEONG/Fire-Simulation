#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "NPC/Decision/YUFSBehaviorDecisionModel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FYUFSCommitProbabilityTest,
	"YUFS.NPC.Decision.CommitProbability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FYUFSCommitProbabilityTest::RunTest(const FString& Parameters)
{
	const TArray<float> Ratios = {1.4f, 2.2f, 1.5f};
	const float Probability = FYUFSBehaviorDecisionModel::ComputeCommitProbability(0.25f, Ratios);
	TestTrue(TEXT("Odds-based probability remains finite and calibrated"), FMath::IsNearlyEqual(Probability, 0.606299f, 0.0001f));
	TestEqual(TEXT("Zero base probability remains zero"), FYUFSBehaviorDecisionModel::ComputeCommitProbability(0.f, Ratios), 0.f);
	TestEqual(TEXT("One base probability remains one"), FYUFSBehaviorDecisionModel::ComputeCommitProbability(1.f, Ratios), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FYUFSDecisionDeterminismTest,
	"YUFS.NPC.Decision.DeterminismAndOrderInvariance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FYUFSDecisionDeterminismTest::RunTest(const FString& Parameters)
{
	const TArray<FYUFSActionWeight> Actions = {
		FYUFSActionWeight(EYUFSAction::SeekInformation, 45.f),
		FYUFSActionWeight(EYUFSAction::WaitForInfo, 20.f),
		FYUFSActionWeight(EYUFSAction::GatherBelongings, 20.f),
		FYUFSActionWeight(EYUFSAction::AlertNearbyOccupants, 10.f),
		FYUFSActionWeight(EYUFSAction::AttemptInitialFirefighting, 5.f)
	};

	FRandomStream FirstActionStream(20260831);
	FRandomStream SecondActionStream(20260831);
	for (int32 Index = 0; Index < 100; ++Index)
	{
		TestEqual(
			TEXT("Same action seed produces the same trace"),
			FYUFSBehaviorDecisionModel::SelectWeightedAction(FirstActionStream, Actions),
			FYUFSBehaviorDecisionModel::SelectWeightedAction(SecondActionStream, Actions));
	}

	const TArray<FYUFSRouteCandidate> ForwardRoutes = {
		FYUFSRouteCandidate(EYUFSRouteStrategy::FamiliarExit, 70.f, -0.1f, true),
		FYUFSRouteCandidate(EYUFSRouteStrategy::CrowdOrLeader, 20.f, 0.2f, true),
		FYUFSRouteCandidate(EYUFSRouteStrategy::NearestSafeExit, 10.f, 0.f, true)
	};
	const TArray<FYUFSRouteCandidate> ReverseRoutes = {
		ForwardRoutes[2], ForwardRoutes[1], ForwardRoutes[0]
	};
	FRandomStream ForwardStream(77);
	FRandomStream ReverseStream(77);
	TestEqual(
		TEXT("Route result is independent of candidate update order"),
		FYUFSBehaviorDecisionModel::SelectRoute(ForwardStream, ForwardRoutes),
		FYUFSBehaviorDecisionModel::SelectRoute(ReverseStream, ReverseRoutes));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FYUFSSafetyFallbackTest,
	"YUFS.NPC.Decision.SafetyFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FYUFSSafetyFallbackTest::RunTest(const FString& Parameters)
{
	const TArray<FYUFSRouteCandidate> UnsafeRoutes = {
		FYUFSRouteCandidate(EYUFSRouteStrategy::FamiliarExit, 70.f, 0.f, false),
		FYUFSRouteCandidate(EYUFSRouteStrategy::CrowdOrLeader, 20.f, 0.f, false),
		FYUFSRouteCandidate(EYUFSRouteStrategy::NearestSafeExit, 10.f, 0.f, false)
	};
	FRandomStream Stream(10);
	TestEqual(
		TEXT("No safe route falls back to shelter in place"),
		FYUFSBehaviorDecisionModel::SelectRoute(Stream, UnsafeRoutes),
		EYUFSRouteStrategy::ShelterInPlace);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FYUFSDistributionCalibrationTest,
	"YUFS.NPC.Decision.DistributionCalibration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FYUFSDistributionCalibrationTest::RunTest(const FString& Parameters)
{
	constexpr int32 SampleCount = 10000;
	FRandomStream CountStream(314159);
	int32 ShortCount = 0;
	int32 MediumCount = 0;
	int32 LongCount = 0;
	for (int32 Index = 0; Index < SampleCount; ++Index)
	{
		const int32 Count = FYUFSBehaviorDecisionModel::SelectPreEvacuationActionCount(
			CountStream, 88.5f, 8.1f, 3.4f);
		if (Count <= 5) ++ShortCount;
		else if (Count <= 9) ++MediumCount;
		else ++LongCount;
	}

	TestTrue(TEXT("1-5 action band is within tolerance"), FMath::Abs(ShortCount / 100.f - 88.5f) < 1.5f);
	TestTrue(TEXT("6-9 action band is within tolerance"), FMath::Abs(MediumCount / 100.f - 8.1f) < 1.5f);
	TestTrue(TEXT("10-15 action band is within tolerance"), FMath::Abs(LongCount / 100.f - 3.4f) < 1.0f);

	const TArray<FYUFSRouteCandidate> Routes = {
		FYUFSRouteCandidate(EYUFSRouteStrategy::FamiliarExit, 70.f, 0.f, true),
		FYUFSRouteCandidate(EYUFSRouteStrategy::CrowdOrLeader, 20.f, 0.f, true),
		FYUFSRouteCandidate(EYUFSRouteStrategy::NearestSafeExit, 10.f, 0.f, true)
	};
	FRandomStream RouteStream(271828);
	int32 FamiliarCount = 0;
	int32 CrowdCount = 0;
	int32 NearestCount = 0;
	for (int32 Index = 0; Index < SampleCount; ++Index)
	{
		switch (FYUFSBehaviorDecisionModel::SelectRoute(RouteStream, Routes))
		{
		case EYUFSRouteStrategy::FamiliarExit: ++FamiliarCount; break;
		case EYUFSRouteStrategy::CrowdOrLeader: ++CrowdCount; break;
		case EYUFSRouteStrategy::NearestSafeExit: ++NearestCount; break;
		default: break;
		}
	}

	TestTrue(TEXT("Familiar route prior is within tolerance"), FMath::Abs(FamiliarCount / 100.f - 70.f) < 2.f);
	TestTrue(TEXT("Crowd route prior is within tolerance"), FMath::Abs(CrowdCount / 100.f - 20.f) < 2.f);
	TestTrue(TEXT("Nearest route prior is within tolerance"), FMath::Abs(NearestCount / 100.f - 10.f) < 2.f);
	return true;
}

#endif
