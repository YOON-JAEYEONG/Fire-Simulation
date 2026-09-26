#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Fire/YUFSBinaryManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSVisualAlignedHazardGridTest,
	"YUFS.Fire.Hazard.BinGridFollowsVisualSVT",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)
bool FYUFSVisualAlignedHazardGridTest::RunTest(const FString&)
{
	// Build the frame transform exactly like UE's OpenVDB importer does for the it_test data:
	// FDS VDB ScaleMap diag(0.4, 0.4, -0.4), sequence bounds min (-12, -2, -5).
	FMatrix VdbMap = FMatrix::Identity;
	VdbMap.M[0][0] = 0.4; VdbMap.M[1][1] = 0.4; VdbMap.M[2][2] = -0.4;
	FTransform Frame(VdbMap);
	const FVector BoundsMin(-12, -2, -5);
	Frame.AddToTranslation(BoundsMin * Frame.GetScale3D());

	const FTransform Component(FRotator(0, 0, 180), FVector(-840, -120, -310), FVector(96, 88, 83));
	const FVector Offset(-4.5, -4.5, -0.5);
	const FTransform Grid = AYUFSBinaryManager::ComputeVisualAlignedGridToWorld(Frame, Component, Offset);

	for (const FVector Cell : { FVector(36, 13, 11), FVector(0, 0, 0), FVector(115.5, 52.5, 19.5) })
	{
		// Reference: the renderer places SVT virtual voxel v at Component(Frame(v)).
		const FVector Virtual = Cell + Offset - BoundsMin;
		const FVector Expected = Component.TransformPosition(Frame.TransformPosition(Virtual));
		const FVector Actual = Grid.TransformPosition(Cell);
		TestTrue(FString::Printf(TEXT("cell %s lands on the rendered voxel"), *Cell.ToCompactString()),
			Actual.Equals(Expected, 0.5));
		TestTrue(FString::Printf(TEXT("cell %s round-trips through the sampler inverse"), *Cell.ToCompactString()),
			Grid.InverseTransformPosition(Actual).Equals(Cell, 0.01));
	}
	// Recovering the sequence bounds from the frame translation is what makes this exact.
	TestTrue(TEXT("bounds recovered from frame translation"),
		(Frame.GetTranslation() / Frame.GetScale3D()).Equals(BoundsMin, 0.001));
	return true;
}
#endif
