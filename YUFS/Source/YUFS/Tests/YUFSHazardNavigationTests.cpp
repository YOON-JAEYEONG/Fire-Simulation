#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Fire/YUFSHazardField.h"
#include "Fire/YUFSBinaryManager.h"
#include "Fire/YUFSHeterogeneousVolume.h"
#include "Engine/World.h"
#include "NPC/Navigation/YUFSSmokeNavigationQueryFilter.h"
#include "NavMesh/RecastHelpers.h"
#include "Detour/DetourNavMeshBuilder.h"

namespace
{
TSharedRef<FYUFSHazardGrid, ESPMode::ThreadSafe> Grid(FIntVector D)
{
	auto Value = MakeShared<FYUFSHazardGrid, ESPMode::ThreadSafe>();
	Value->Dimensions = D;
	Value->Density.Init(0, D.X * D.Y * D.Z);
	Value->Temperature.Init(0, D.X * D.Y * D.Z);
	return Value;
}
FYUFSHazardSnapshot Snapshot(TSharedRef<FYUFSHazardGrid, ESPMode::ThreadSafe> Data,
	FTransform Transform = FTransform(FQuat::Identity, FVector::ZeroVector, FVector(100)))
{
	FYUFSHazardSnapshot Value;
	Value.Grid = Data;
	Value.GridToWorld = Transform;
	Value.Status = EYUFSHazardDataStatus::Ready;
	Value.Frame = 0;
	return Value;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSHazardCoordinatesTest, "YUFS.NPC.Navigation.Hazard.TransformAndFloors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSHazardCoordinatesTest::RunTest(const FString&)
{
	auto Data = Grid(FIntVector(3, 2, 8));
	const int32 HotCell = (1 * 2 + 1) * 8 + 1;
	Data->Density[HotCell] = 255;
	Data->Temperature[HotCell] = 128;
	const FTransform Transform(FRotator(0, 180, 0), FVector(6393, -1154, 0), FVector(40, -60, 40));
	auto Field = Snapshot(Data, Transform);
	const FVector FirstFloor = Transform.TransformPosition(FVector(1.5, 1.5, 1.5));
	TestEqual(TEXT("Rotation and reflected Y preserve voxel selection"), Field.Sample(FirstFloor).Smoke, 1.f);
	TestEqual(TEXT("Same XY upstairs samples a different Z cell"),
		Field.Sample(FirstFloor + FVector(0, 0, 160)).Smoke, 0.f);
	TestEqual(TEXT("Outside is distinct from a zero-density cell"),
		Field.Sample(Transform.TransformPosition(FVector(-0.01, 1, 1))).Status, EYUFSHazardDataStatus::OutsideDomain);
	Field.GridToWorld.SetScale3D(FVector(0, 1, 1));
	TestEqual(TEXT("Degenerate transforms are rejected"), Field.Sample(FirstFloor).Status, EYUFSHazardDataStatus::InvalidMapping);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSHazardCostTest, "YUFS.NPC.Navigation.Hazard.SmokeHeatAndEscape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSHazardCostTest::RunTest(const FString&)
{
	auto Data = Grid(FIntVector(6, 4, 3));
	for (int32 X = 2; X < 4; ++X)
	for (int32 Z = 0; Z < 3; ++Z)
		Data->Density[(X * 4 + 1) * 3 + Z] = 255;
	auto Field = Snapshot(Data);
	FYUFSHazardSettings Settings;
	const FVector A(50, 150, 0), B(550, 150, 0);
	const TArray<FVector> Direct{A, B}, Detour{A, FVector(50, 350, 0), FVector(550, 350, 0), B};
	const auto Unsafe = Field.ScorePath(Direct, Settings), Safe = Field.ScorePath(Detour, Settings);
	TestTrue(TEXT("Smoke makes the short route costlier than the long safe route"), Unsafe.Length + Unsafe.AddedCost > Safe.Length + Safe.AddedCost);
	TestTrue(TEXT("Entering dangerous smoke is refused"), Unsafe.bUnsafeAhead);
	TestFalse(TEXT("Long safe route remains usable"), Safe.bUnsafeAhead);
	TestFalse(TEXT("An NPC already in danger may leave it"),
		Field.ScorePath({FVector(250, 150, 0), A}, Settings).bUnsafeAhead);
	TestTrue(TEXT("Leaving and re-entering danger is refused"),
		Field.ScorePath({FVector(250, 150, 0), A, B}, Settings).bUnsafeAhead);
	for (int32 I = 0; I < Data->Density.Num(); ++I)
	{
		Data->Temperature[I] = Data->Density[I];
		Data->Density[I] = 0;
	}
	TestTrue(TEXT("Heat alone changes travel cost"), Field.SegmentAddedCost(A, B, Settings) > 0);
	TestTrue(TEXT("Heat alone blocks unsafe entry"), Field.ScorePath(Direct, Settings).bUnsafeAhead);
	for (int32 I = 0; I < Data->Temperature.Num(); ++I)
		if (I % 3 != 0) Data->Temperature[I] = 0;
	TestTrue(TEXT("Ground fire is detected even when head-height samples are cold"),
		Field.ScorePath(Direct, Settings).bUnsafeAhead);
	return true;
}

// Check that replacing a streaming slot cannot change a field already handed to a navigation worker.
struct FYUFSHazardTestAccess
{
	static void Initialize(AYUFSBinaryManager* Manager, AYUFSHeterogeneousVolume* Volume)
	{
		Manager->bHeaderValid = true; Manager->TotalFrames = 1000;
		Manager->DimX = 1; Manager->DimY = 1; Manager->DimZ = 1;
		Manager->HeterogeneousVolume = Volume;
		Manager->FramesBuffer.SetNum(Manager->MaxBufferSize);
		Manager->LoadedFrameIndices.Init(INDEX_NONE, Manager->MaxBufferSize);
	}
	static void Publish(AYUFSBinaryManager* Manager, int32 Frame, TSharedRef<FYUFSHazardGrid, ESPMode::ThreadSafe> Data)
	{
		Manager->FramesBuffer[Frame % Manager->MaxBufferSize] = Data;
		Manager->LoadedFrameIndices[Frame % Manager->MaxBufferSize] = Frame;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSHazardStreamingTest, "YUFS.NPC.Navigation.Hazard.StreamingSnapshotIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSHazardStreamingTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	auto* Manager = World->SpawnActor<AYUFSBinaryManager>();
	auto* Volume = World->SpawnActor<AYUFSHeterogeneousVolume>();
	TestEqual(TEXT("Missing header is never Ready"), Manager->GetHazardSnapshot(0).Status, EYUFSHazardDataStatus::MissingData);
	FYUFSHazardTestAccess::Initialize(Manager, Volume);
	TestEqual(TEXT("An unloaded valid frame is Loading"), Manager->GetHazardSnapshot(0).Status, EYUFSHazardDataStatus::Loading);
	TestEqual(TEXT("An invalid frame is distinct"), Manager->GetHazardSnapshot(1000).Status, EYUFSHazardDataStatus::InvalidFrame);
	auto OldGrid = Grid(FIntVector(1)); OldGrid->Density[0] = 255;
	FYUFSHazardTestAccess::Publish(Manager, 0, OldGrid);
	const auto OldSnapshot = Manager->GetHazardSnapshot(0);
	FYUFSHazardTestAccess::Publish(Manager, 192, Grid(FIntVector(1)));
	TestEqual(TEXT("Overwritten slot no longer serves an old frame"), Manager->GetHazardSnapshot(0).Status, EYUFSHazardDataStatus::Loading);
	TestEqual(TEXT("In-flight query keeps immutable old bytes"), OldSnapshot.Sample(FVector(20)).Smoke, 1.f);
	TestEqual(TEXT("New frame sees its own bytes"), Manager->GetHazardSnapshot(192).Sample(FVector(20)).Smoke, 0.f);
	World->DestroyWorld(false);
	return true;
}

#if WITH_RECAST
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSHazardDetourTest, "YUFS.NPC.Navigation.Hazard.ActualDetourChoosesOtherCorridor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSHazardDetourTest::RunTest(const FString&)
{
	// Eight connected squares around a solid central obstacle, with two alternative corridors.
	TArray<unsigned short> Verts, Polys, Flags;
	TArray<unsigned char> Areas;
	for (int32 Z = 0; Z < 4; ++Z)
	for (int32 X = 0; X < 4; ++X)
	{
		Verts.Add(X); Verts.Add(0); Verts.Add(Z);
	}
	for (int32 Z = 0; Z < 3; ++Z)
	for (int32 X = 0; X < 3; ++X)
	{
		if (X == 1 && Z == 1) continue;
		Polys.Append({static_cast<unsigned short>(Z * 4 + X), static_cast<unsigned short>((Z + 1) * 4 + X),
			static_cast<unsigned short>((Z + 1) * 4 + X + 1), static_cast<unsigned short>(Z * 4 + X + 1),
			0xffff, 0xffff, 0xffff, 0xffff});
		Flags.Add(1); Areas.Add(1);
	}
	for (int32 A = 0; A < 8; ++A)
	for (int32 Edge = 0; Edge < 4; ++Edge)
	for (int32 B = 0; B < 8; ++B)
	for (int32 OtherEdge = 0; OtherEdge < 4; ++OtherEdge)
	{
		if (A != B && Polys[A * 8 + Edge] == Polys[B * 8 + (OtherEdge + 1) % 4] &&
			Polys[A * 8 + (Edge + 1) % 4] == Polys[B * 8 + OtherEdge])
			Polys[A * 8 + 4 + Edge] = B;
	}
	dtNavMeshCreateParams Params{};
	Params.verts = Verts.GetData(); Params.vertCount = 16;
	Params.polys = Polys.GetData(); Params.polyCount = 8; Params.nvp = 4;
	Params.polyFlags = Flags.GetData(); Params.polyAreas = Areas.GetData();
	Params.cs = 100; Params.ch = 100; Params.bmax[0] = 300; Params.bmax[1] = 100; Params.bmax[2] = 300;
	Params.walkableHeight = 180; Params.walkableRadius = 30; Params.walkableClimb = 45;
	Params.buildBvTree = true;
	unsigned char* NavBytes = nullptr; int ByteCount = 0;
	if (!TestTrue(TEXT("Construct a real Detour tile"), dtCreateNavMeshData(&Params, &NavBytes, &ByteCount))) return false;
	dtNavMesh Mesh;
	dtNavMeshParams MeshParams{};
	MeshParams.tileWidth = 300; MeshParams.tileHeight = 300;
	MeshParams.maxTiles = 1; MeshParams.maxPolys = 8;
	MeshParams.walkableHeight = 180; MeshParams.walkableRadius = 30; MeshParams.walkableClimb = 45;
	for (auto& Resolution : MeshParams.resolutionParams) Resolution.bvQuantFactor = 0.01;
	if (!TestTrue(TEXT("Load tile"), dtStatusSucceed(Mesh.init(&MeshParams)) &&
		dtStatusSucceed(Mesh.addTile(NavBytes, ByteCount, DT_TILE_FREE_DATA, 0, nullptr))))
	{
		dtFree(NavBytes, DT_ALLOC_PERM_TILE_DATA); return false;
	}
	dtNavMeshQuery Query;
	if (!TestTrue(TEXT("Initialize query"), dtStatusSucceed(Query.init(&Mesh, 128)))) return false;
	FRecastQueryFilter Base;
	Base.SetAreaCost(1, 1);
	const dtReal A[3] = {50, 0, 150}, B[3] = {250, 0, 150}, Extent[3] = {20, 20, 20};
	dtPolyRef StartRef = 0, EndRef = 0;
	Query.findNearestPoly(A, Extent, &Base, &StartRef, nullptr);
	Query.findNearestPoly(B, Extent, &Base, &EndRef, nullptr);
	if (!TestTrue(TEXT("Endpoints are on navigation polygons"), StartRef != 0 && EndRef != 0)) return false;
	dtQueryResult Original;
	dtReal Cost = 0;
	Query.findPath(StartRef, EndRef, A, B, MAX_flt, &Base, Original, &Cost);
	if (!TestTrue(TEXT("Baseline path reaches the destination"), Original.size() >= 5 && Original.getRef(Original.size() - 1) == EndRef)) return false;
	const dtPolyRef HazardRef = Original.getRef(2);
	const dtMeshTile* Tile = nullptr; const dtPoly* Poly = nullptr;
	Mesh.getTileAndPolyByRef(HazardRef, &Tile, &Poly);
	FVector Center = FVector::ZeroVector;
	for (int32 I = 0; I < Poly->vertCount; ++I) Center += Recast2UnrealPoint(&Tile->verts[Poly->verts[I] * 3]);
	Center /= Poly->vertCount;
	auto HotGrid = Grid(FIntVector(3, 3, 3));
	auto HotField = Snapshot(HotGrid, FTransform(FQuat::Identity, FVector(-300, -300, -50), FVector(100)));
	const FVector Cell = HotField.GridToWorld.InverseTransformPosition(Center);
	const int32 X = FMath::FloorToInt(Cell.X), Y = FMath::FloorToInt(Cell.Y);
	for (int32 Z = 0; Z < 3; ++Z) HotGrid->Density[(X * 3 + Y) * 3 + Z] = 255;
	FYUFSHazardSettings Settings;
	FYUFSHazardRecastFilter HotFilter(Base, HotField, Settings);
	// Verify the copy used by FNavigationQueryFilter preserves virtual dispatch and the grid.
	TUniquePtr<INavigationQueryFilterInterface> Copy(HotFilter.CreateCopy());
	auto* Copied = static_cast<FYUFSHazardRecastFilter*>(Copy.Get());
	dtQueryResult Detour;
	Query.findPath(StartRef, EndRef, A, B, MAX_flt, Copied, Detour, &Cost);
	bool bUsesHazard = false;
	for (int32 I = 0; I < Detour.size(); ++I) bUsesHazard |= Detour.getRef(I) == HazardRef;
	TestTrue(TEXT("Hazard-aware path still reaches destination"), Detour.size() > 0 && Detour.getRef(Detour.size() - 1) == EndRef);
	TestFalse(TEXT("Actual A* chooses the other corridor without any NavArea_Obstacle"), bUsesHazard);
	dtQueryResult OriginalAgain;
	Query.findPath(StartRef, EndRef, A, B, MAX_flt, &Base, OriginalAgain, &Cost);
	TestEqual(TEXT("Another NPC's default query was not mutated"), OriginalAgain.getRef(2), HazardRef);
	return true;
}
#endif
#endif
