#include "NPC/Navigation/YUFSSmokeNavigationQueryFilter.h"
#include "NavigationData.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavMesh/RecastHelpers.h"

#if WITH_RECAST
FYUFSHazardRecastFilter::FYUFSHazardRecastFilter(const FRecastQueryFilter& Base,
	const FYUFSHazardSnapshot& InSnapshot, const FYUFSHazardSettings& InSettings)
	: FRecastQueryFilter(Base), Snapshot(InSnapshot), Settings(InSettings)
{
	SetIsVirtual(true);
}

dtReal FYUFSHazardRecastFilter::getVirtualCost(const dtReal* A, const dtReal* B,
	dtPolyRef PrevRef, const dtMeshTile* PrevTile, const dtPoly* PrevPoly,
	dtPolyRef CurRef, const dtMeshTile* CurTile, const dtPoly* CurPoly,
	dtPolyRef NextRef, const dtMeshTile* NextTile, const dtPoly* NextPoly) const
{
	const dtReal Base = FRecastQueryFilter::getVirtualCost(A, B,
		PrevRef, PrevTile, PrevPoly, CurRef, CurTile, CurPoly, NextRef, NextTile, NextPoly);
	return Base + Snapshot.SegmentAddedCost(Recast2UnrealPoint(A), Recast2UnrealPoint(B), Settings);
}
#endif

FSharedConstNavQueryFilter UYUFSSmokeNavigationQueryFilter::CreateQueryFilter(
	const ANavigationData& NavData, const UObject* Querier,
	const FYUFSHazardSnapshot& Snapshot, const FYUFSHazardSettings& Settings)
{
#if WITH_RECAST
	if (!Cast<ARecastNavMesh>(&NavData)) return nullptr;
	const FSharedConstNavQueryFilter Base = GetQueryFilter(NavData, Querier, StaticClass());
	if (!Base.IsValid() || !Base->GetImplementation()) return nullptr;
	FSharedNavQueryFilter Filter = Base->GetCopy();
	const FYUFSHazardRecastFilter HazardFilter(
		*static_cast<const FRecastQueryFilter*>(Base->GetImplementation()), Snapshot, Settings);
	Filter->SetFilterImplementation(&HazardFilter);
	return Filter;
#else
	return nullptr;
#endif
}
