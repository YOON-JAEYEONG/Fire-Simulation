#if WITH_DEV_AUTOMATION_TESTS

#include "Core/YUFSRouteAssignment.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FYUFSRouteAssignmentExactRatioTest,
	"YUFS.NPC.RouteAssignment.Exact70_20_10",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FYUFSRouteAssignmentExactRatioTest::RunTest(const FString& Parameters)
{
	const FYUFSRoutePreferenceCounts Counts =
		FYUFSRouteAssignment::CalculateCounts(100, 0.70f, 0.20f, 0.10f);

	TestEqual(TEXT("Familiar count"), Counts.FamiliarExit, 70);
	TestEqual(TEXT("Social count"), Counts.SocialFollowing, 20);
	TestEqual(TEXT("Nearest count"), Counts.NearestSafeExit, 10);
	TestEqual(TEXT("Total count"), Counts.Total(), 100);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FYUFSRouteAssignmentRoundingTest,
	"YUFS.NPC.RouteAssignment.PreservesArbitraryTotal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FYUFSRouteAssignmentRoundingTest::RunTest(const FString& Parameters)
{
	const FYUFSRoutePreferenceCounts Counts =
		FYUFSRouteAssignment::CalculateCounts(23, 0.70f, 0.20f, 0.10f);

	TestEqual(TEXT("Familiar count"), Counts.FamiliarExit, 16);
	TestEqual(TEXT("Social count"), Counts.SocialFollowing, 5);
	TestEqual(TEXT("Nearest count"), Counts.NearestSafeExit, 2);
	TestEqual(TEXT("Total count"), Counts.Total(), 23);
	return true;
}

#endif
