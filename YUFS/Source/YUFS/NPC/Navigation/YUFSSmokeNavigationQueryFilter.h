#pragma once

#include "CoreMinimal.h"
#include "Fire/YUFSHazardField.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "NavMesh/RecastQueryFilter.h"
#include "YUFSSmokeNavigationQueryFilter.generated.h"

#if WITH_RECAST
class FYUFSHazardRecastFilter final : public FRecastQueryFilter
{
public:
	FYUFSHazardRecastFilter(const FRecastQueryFilter& Base, const FYUFSHazardSnapshot& InSnapshot,
		const FYUFSHazardSettings& InSettings);
	virtual INavigationQueryFilterInterface* CreateCopy() const override { return new FYUFSHazardRecastFilter(*this); }
	virtual bool IsEqual(const INavigationQueryFilterInterface* Other) const override { return this == Other; }
	virtual void Reset() override { FRecastQueryFilter::Reset(); SetIsVirtual(true); }
protected:
	virtual dtReal getVirtualCost(const dtReal* A, const dtReal* B,
		dtPolyRef PrevRef, const dtMeshTile* PrevTile, const dtPoly* PrevPoly,
		dtPolyRef CurRef, const dtMeshTile* CurTile, const dtPoly* CurPoly,
		dtPolyRef NextRef, const dtMeshTile* NextTile, const dtPoly* NextPoly) const override;
private:
	FYUFSHazardSnapshot Snapshot;
	FYUFSHazardSettings Settings;
};
#endif

UCLASS()
class YUFS_API UYUFSSmokeNavigationQueryFilter : public UNavigationQueryFilter
{
	GENERATED_BODY()
public:
	// Snapshot data only: the engine may evaluate this filter on a navigation worker.
	static FSharedConstNavQueryFilter CreateQueryFilter(const ANavigationData& NavData,
		const UObject* Querier, const FYUFSHazardSnapshot& Snapshot, const FYUFSHazardSettings& Settings);
};
