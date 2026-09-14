#include "NPC/Navigation/YUFSSmokeNavigationQueryFilter.h"
#include "NavigationData.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavMesh/RecastHelpers.h"

FVector2D FYUFSObservedHazardSnapshot::Sample(const FVector& WorldPoint) const
{
 FVector2D Result = FVector2D::ZeroVector;
 for (const auto& Known : Samples)
 {
  if (FVector::DistSquared(Known.Location, WorldPoint) > FMath::Square(Known.RadiusCm)) continue;
  Result.X = FMath::Max(Result.X, double(Known.Smoke));
  Result.Y = FMath::Max(Result.Y, double(Known.Heat));
 }
 return Result;
}
bool FYUFSObservedHazardSnapshot::IsLocationSafe(const FVector& WorldPoint) const
{
 const auto Risk = Sample(WorldPoint);
 return Risk.X < BlockSmoke && Risk.Y < BlockHeat;
}
float FYUFSObservedHazardSnapshot::SegmentAddedCost(const FVector& A, const FVector& B) const
{
 const float Length = FVector::Dist(A, B);
 if (Samples.IsEmpty() || Length <= UE_SMALL_NUMBER) return 0.f;
 const int32 Steps = FMath::Clamp(FMath::CeilToInt(Length / 20.f), 1, 4096);
 float Total = 0.f;
 for (int32 I = 0; I <= Steps; ++I)
 {
  const auto Risk = Sample(FMath::Lerp(A, B, float(I) / Steps) + FVector(0, 0, SampleHeightCm));
  Total += float(Risk.X) * SmokeCost + float(Risk.Y) * HeatCost;
 }
 return Length * Total / (Steps + 1);
}
bool FYUFSObservedHazardSnapshot::IsPathSafe(const TArray<FVector>& FloorPoints) const
{
 for (int32 Segment = 1; Segment < FloorPoints.Num(); ++Segment)
 {
  const FVector& A = FloorPoints[Segment - 1];
  const FVector& B = FloorPoints[Segment];
  const int32 Steps = FMath::Clamp(FMath::CeilToInt(FVector::Dist(A, B) / 20.f), 1, 4096);
  for (int32 I = 0; I <= Steps; ++I)
   if (!IsLocationSafe(FMath::Lerp(A, B, float(I) / Steps) + FVector(0, 0, SampleHeightCm))) return false;
 }
 return true; // Unobserved means unknown, not certified safe.
}
#if WITH_RECAST
FYUFSObservedHazardRecastFilter::FYUFSObservedHazardRecastFilter(const FRecastQueryFilter& Base,
 const FYUFSObservedHazardSnapshot& InSnapshot) : FRecastQueryFilter(Base), Snapshot(InSnapshot)
{
 SetIsVirtual(true);
}
dtReal FYUFSObservedHazardRecastFilter::getVirtualCost(const dtReal* A, const dtReal* B,
 dtPolyRef PrevRef, const dtMeshTile* PrevTile, const dtPoly* PrevPoly,
 dtPolyRef CurRef, const dtMeshTile* CurTile, const dtPoly* CurPoly,
 dtPolyRef NextRef, const dtMeshTile* NextTile, const dtPoly* NextPoly) const
{
 return FRecastQueryFilter::getVirtualCost(A, B, PrevRef, PrevTile, PrevPoly,
  CurRef, CurTile, CurPoly, NextRef, NextTile, NextPoly)
  + Snapshot.SegmentAddedCost(Recast2UnrealPoint(A), Recast2UnrealPoint(B));
}
#endif
FSharedConstNavQueryFilter UYUFSSmokeNavigationQueryFilter::CreateQueryFilter(
 const ANavigationData& NavData, const UObject* Querier, const FYUFSObservedHazardSnapshot& Snapshot)
{
 const auto Base = GetQueryFilter(NavData, Querier, StaticClass());
 if (!Base.IsValid() || !Base->GetImplementation()) return nullptr;
 FSharedNavQueryFilter Filter = Base->GetCopy();
#if WITH_RECAST
 if (Cast<ARecastNavMesh>(&NavData))
 {
  const FYUFSObservedHazardRecastFilter ObservedFilter(
   *static_cast<const FRecastQueryFilter*>(Base->GetImplementation()), Snapshot);
  Filter->SetFilterImplementation(&ObservedFilter);
  return Filter;
 }
#endif
 // Unsupported backends cannot silently ignore already-known danger.
 return Snapshot.Samples.IsEmpty() ? Filter : nullptr;
}
