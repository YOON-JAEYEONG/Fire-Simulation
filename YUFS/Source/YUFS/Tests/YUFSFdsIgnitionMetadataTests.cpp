#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Fire/YUFSFdsIgnitionMetadata.h"
#include <limits>

namespace
{
	FString ValidFdsIgnitionJson()
	{
		// Synthetic, asset-free parser fixture. Never installed as a runtime target.
		return TEXT(R"json({
  "schemaVersion": 1,
  "sourceFile": "test-fixture.fds",
  "sourceSha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "alignmentConfirmed": true,
  "units": "m",
  "coordinateMapping": {
    "originUeCm": [10, 20, 30],
    "axisX": [0, 1, 0],
    "axisY": [-1, 0, 0],
    "axisZ": [0, 0, -1],
    "scaleCmPerMeter": [100, 200, 50]
  },
  "timeMapping": {
    "referenceClock": "simulationElapsedSeconds",
    "fdsTimeAtSimulationZeroSeconds": -2,
    "fdsSecondsPerSimulationSecond": 2
  },
  "selectedIgnitionId": "burner-A",
  "ignitionSources": [
    {"id": "burner-A", "fdsPositionMeters": [1, 2, 3], "activationTimeSeconds": 4}
  ]
})json");
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FYUFSFdsExplicitTransformTest,
	"YUFS.Fire.FdsIgnitionMetadata.ExplicitTransformAndTime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FYUFSFdsExplicitTransformTest::RunTest(const FString& Parameters)
{
	FYUFSFdsIgnitionMetadata Metadata;
	FString Reason;
	if (!TestTrue(TEXT("explicit reflected, non-uniform mapping accepted"),
		FYUFSFdsIgnitionMetadata::ParseJson(ValidFdsIgnitionJson(), Metadata, Reason))) return false;
	TestEqual(TEXT("selected source has stable identity"), Metadata.GetSelectedIgnitionId(), FString(TEXT("burner-A")));
	FVector Target(999.0);
	TestFalse(TEXT("explicit time mapping delays activation until simulation t=3"), Metadata.TryGetActiveIgnition(2.999, Target, Reason));
	TestTrue(TEXT("inactive query clears any stale location"), Target.IsZero());
	TestTrue(TEXT("activation boundary is inclusive"), Metadata.TryGetActiveIgnition(3.0, Target, Reason));
	TestTrue(TEXT("origin + declared axes/scales only; no extra actor transform or inferred flip"), Target.Equals(FVector(-390, 120, -120), 1.e-6));
	TestFalse(TEXT("negative clock rejected"), Metadata.TryGetActiveIgnition(-1.0, Target, Reason));
	TestFalse(TEXT("NaN clock rejected"), Metadata.TryGetActiveIgnition(std::numeric_limits<double>::quiet_NaN(), Target, Reason));
	TestFalse(TEXT("infinite clock rejected"), Metadata.TryGetActiveIgnition(std::numeric_limits<double>::infinity(), Target, Reason));
	TestTrue(TEXT("rejected clock never leaves target output"), Target.IsZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FYUFSFdsInvalidMappingTest,
	"YUFS.Fire.FdsIgnitionMetadata.RejectUnknownAlignment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FYUFSFdsInvalidMappingTest::RunTest(const FString& Parameters)
{
	struct FInvalidCase { const TCHAR* Name; const TCHAR* From; const TCHAR* To; };
	const FInvalidCase Cases[] = {
		{TEXT("unsupported schema"), TEXT("\"schemaVersion\": 1"), TEXT("\"schemaVersion\": 2")},
		{TEXT("schema cannot be coerced from boolean"), TEXT("\"schemaVersion\": 1"), TEXT("\"schemaVersion\": true")},
		{TEXT("schema cannot be coerced from string"), TEXT("\"schemaVersion\": 1"), TEXT("\"schemaVersion\": \"1\"")},
		{TEXT("unconfirmed alignment"), TEXT("\"alignmentConfirmed\": true"), TEXT("\"alignmentConfirmed\": false")},
		{TEXT("alignment cannot be coerced from numeric truthiness"), TEXT("\"alignmentConfirmed\": true"), TEXT("\"alignmentConfirmed\": 1")},
		{TEXT("missing alignment"), TEXT("\"alignmentConfirmed\": true,"), TEXT("")},
		{TEXT("wrong units"), TEXT("\"units\": \"m\""), TEXT("\"units\": \"cm\"")},
		{TEXT("missing provenance hash"), TEXT("\"sourceSha256\""), TEXT("\"notTheSourceHash\"")},
		{TEXT("malformed provenance hash"), TEXT("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"), TEXT("unknown")},
		{TEXT("missing scale must not default to 100"), TEXT("\"scaleCmPerMeter\""), TEXT("\"notTheScale\"")},
		{TEXT("zero scale"), TEXT("[100, 200, 50]"), TEXT("[100, 0, 50]")},
		{TEXT("negative scale is not an implicit axis flip"), TEXT("[100, 200, 50]"), TEXT("[100, -200, 50]")},
		{TEXT("non-unit axis"), TEXT("\"axisX\": [0, 1, 0]"), TEXT("\"axisX\": [0, 2, 0]")},
		{TEXT("parallel axes"), TEXT("\"axisY\": [-1, 0, 0]"), TEXT("\"axisY\": [0, 1, 0]")},
		{TEXT("missing axis convention"), TEXT("\"axisZ\""), TEXT("\"notTheAxis\"")},
		{TEXT("nonnumeric coordinate"), TEXT("\"originUeCm\": [10, 20, 30]"), TEXT("\"originUeCm\": [\"10\", 20, 30]")},
		{TEXT("non-finite number"), TEXT("\"originUeCm\": [10, 20, 30]"), TEXT("\"originUeCm\": [1e309, 20, 30]")},
		{TEXT("world transform overflow"), TEXT("\"fdsPositionMeters\": [1, 2, 3]"), TEXT("\"fdsPositionMeters\": [1e308, 2, 3]")},
		{TEXT("missing time mapping"), TEXT("\"timeMapping\""), TEXT("\"notTheTimeMapping\"")},
		{TEXT("volume FPS is not a FDS clock"), TEXT("simulationElapsedSeconds"), TEXT("volumePlaybackSeconds")},
		{TEXT("invalid time scale"), TEXT("\"fdsSecondsPerSimulationSecond\": 2"), TEXT("\"fdsSecondsPerSimulationSecond\": 0")},
		{TEXT("missing activation time"), TEXT("\"activationTimeSeconds\""), TEXT("\"notTheActivation\"")}
	};
	for (const FInvalidCase& Case : Cases)
	{
		FYUFSFdsIgnitionMetadata Metadata;
		FString Reason;
		TestTrue(TEXT("seed a valid old result before invalid parse"), FYUFSFdsIgnitionMetadata::ParseJson(ValidFdsIgnitionJson(), Metadata, Reason));
		const FString InvalidJson = ValidFdsIgnitionJson().Replace(Case.From, Case.To);
		TestFalse(Case.Name, FYUFSFdsIgnitionMetadata::ParseJson(InvalidJson, Metadata, Reason));
		TestFalse(TEXT("failure discards previous valid metadata"), Metadata.IsValid());
		TestFalse(TEXT("failure includes diagnostic reason"), Reason.IsEmpty());
		FVector Target(999.0);
		TestFalse(TEXT("invalid mapping cannot supply a fallback target"), Metadata.TryGetActiveIgnition(100.0, Target, Reason));
		TestTrue(TEXT("failed query clears target output"), Target.IsZero());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FYUFSFdsSourceSelectionTest,
	"YUFS.Fire.FdsIgnitionMetadata.SourceSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FYUFSFdsSourceSelectionTest::RunTest(const FString& Parameters)
{
	const FString SingleUnselected = ValidFdsIgnitionJson().Replace(TEXT("\"selectedIgnitionId\": \"burner-A\","), TEXT(""));
	const FString TwoSources = SingleUnselected.Replace(
		TEXT("\"activationTimeSeconds\": 4}"),
		TEXT("\"activationTimeSeconds\": 4}, {\"id\": \"burner-B\", \"fdsPositionMeters\": [9, 8, 7], \"activationTimeSeconds\": 0}"));
	FYUFSFdsIgnitionMetadata Metadata;
	FString Reason;
	TestTrue(TEXT("one source is unambiguous without selection"), FYUFSFdsIgnitionMetadata::ParseJson(SingleUnselected, Metadata, Reason));
	TestFalse(TEXT("multiple sources never choose nearest, first or hottest implicitly"), FYUFSFdsIgnitionMetadata::ParseJson(TwoSources, Metadata, Reason));
	TestEqual(TEXT("ambiguous source diagnostic"), Reason, FString(TEXT("FdsMetadata.AmbiguousIgnitionSource")));
	const FString Selected = TwoSources.Replace(TEXT("\"ignitionSources\":"), TEXT("\"selectedIgnitionId\": \"burner-B\", \"ignitionSources\":"));
	TestTrue(TEXT("explicit multi-source selection accepted"), FYUFSFdsIgnitionMetadata::ParseJson(Selected, Metadata, Reason));
	TestEqual(TEXT("selection preserves actual identifier"), Metadata.GetSelectedIgnitionId(), FString(TEXT("burner-B")));
	const FString MissingId = Selected.Replace(TEXT("\"selectedIgnitionId\": \"burner-B\""), TEXT("\"selectedIgnitionId\": \"unknown\""));
	TestFalse(TEXT("unknown selection never falls back to another source"), FYUFSFdsIgnitionMetadata::ParseJson(MissingId, Metadata, Reason));
	const FString Duplicate = Selected.Replace(TEXT("burner-B"), TEXT("burner-A"));
	TestFalse(TEXT("duplicate source IDs rejected"), FYUFSFdsIgnitionMetadata::ParseJson(Duplicate, Metadata, Reason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FYUFSFdsMissingFileTest,
	"YUFS.Fire.FdsIgnitionMetadata.MissingFileFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FYUFSFdsMissingFileTest::RunTest(const FString& Parameters)
{
	FYUFSFdsIgnitionMetadata Metadata;
	FString Reason;
	TestTrue(TEXT("seed valid metadata"), FYUFSFdsIgnitionMetadata::ParseJson(ValidFdsIgnitionJson(), Metadata, Reason));
	TestFalse(TEXT("empty file path rejected without creating data"), FYUFSFdsIgnitionMetadata::LoadFile(TEXT(""), Metadata, Reason));
	TestFalse(TEXT("load failure clears prior metadata"), Metadata.IsValid());
	FVector Target(999.0);
	TestFalse(TEXT("missing file cannot activate previous or guessed position"), Metadata.TryGetActiveIgnition(100, Target, Reason));
	TestTrue(TEXT("missing source yields no target"), Target.IsZero());
	TestFalse(TEXT("malformed JSON rejected"), FYUFSFdsIgnitionMetadata::ParseJson(TEXT("{"), Metadata, Reason));
	return true;
}

#endif
