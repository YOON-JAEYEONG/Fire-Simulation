#pragma once
#include "CoreMinimal.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "NavMesh/RecastQueryFilter.h"
#include "YUFSSmokeNavigationQueryFilter.generated.h"

// Explicitly observed location; normalized exported heat is NOT Celsius.
struct FYUFSObservedHazard
{
 FVector Location = FVector::ZeroVector;
 float Smoke = 0.f, Heat = 0.f, RadiusCm = 40.f, ObservedAt = 0.f;
};

// Value copy per query. Workers never access actors or mutable perception memory.
struct FYUFSObservedHazardSnapshot
{
 TArray<FYUFSObservedHazard> Samples;
 float SmokeCost = 30.f, HeatCost = 60.f;
 float BlockSmoke = 0.7f, BlockHeat = 0.7f, SampleHeightCm = 120.f;
 float SegmentAddedCost(const FVector& A, const FVector& B) const;
 bool IsPathSafe(const TArray<FVector>& FloorPoints) const;
 bool IsLocationSafe(const FVector& WorldPoint) const;
 FVector2D Sample(const FVector& WorldPoint) const;
};

#if WITH_RECAST
class FYUFSObservedHazardRecastFilter final : public FRecastQueryFilter
{
public:
 FYUFSObservedHazardRecastFilter(const FRecastQueryFilter& Base, const FYUFSObservedHazardSnapshot& InSnapshot);
 virtual INavigationQueryFilterInterface* CreateCopy() const override { return new FYUFSObservedHazardRecastFilter(*this); }
 virtual bool IsEqual(const INavigationQueryFilterInterface* Other) const override { return this == Other; }
 virtual void Reset() override { FRecastQueryFilter::Reset(); SetIsVirtual(true); }
protected:
 virtual dtReal getVirtualCost(const dtReal* A, const dtReal* B,
  dtPolyRef PrevRef, const dtMeshTile* PrevTile, const dtPoly* PrevPoly,
  dtPolyRef CurRef, const dtMeshTile* CurTile, const dtPoly* CurPoly,
  dtPolyRef NextRef, const dtMeshTile* NextTile, const dtPoly* NextPoly) const override;
private:
 FYUFSObservedHazardSnapshot Snapshot;
};
#endif

UCLASS()
class YUFS_API UYUFSSmokeNavigationQueryFilter : public UNavigationQueryFilter
{
 GENERATED_BODY()
public:
 static FSharedConstNavQueryFilter CreateQueryFilter(const ANavigationData& NavData,
  const UObject* Querier, const FYUFSObservedHazardSnapshot& Snapshot);
};
