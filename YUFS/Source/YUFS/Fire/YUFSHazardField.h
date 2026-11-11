#pragma once

#include "CoreMinimal.h"
#include "YUFSHazardField.generated.h"

UENUM(BlueprintType)
enum class EYUFSHazardDataStatus : uint8
{
	Ready, OutsideDomain, Loading, MissingData, InvalidFrame, InvalidMapping
};

// Published grids are immutable. Navigation workers never access an actor or a ring buffer.
struct FYUFSHazardGrid
{
	FIntVector Dimensions = FIntVector::ZeroValue;
	TArray<uint8> Density;
	TArray<uint8> Temperature;
};

struct FYUFSHazardSample
{
	EYUFSHazardDataStatus Status = EYUFSHazardDataStatus::MissingData;
	float Smoke = 0.f;
	float Heat = 0.f; // Normalized exported byte, NOT degrees Celsius.
};

struct FYUFSHazardSettings
{
	float SmokeCost = 30.f;
	float HeatCost = 60.f;
	float SampleStepCm = 20.f;
	float SampleHeightCm = 120.f; // Relative to the walking surface.
	float BodyHeatHeightCm = 0.f; // Optional extra heat probe at the top of a lowered collision body.
	float BlockSmoke = 0.7f;
	float BlockHeat = 0.7f;

	// Shared by scoring and rejected-route witnesses; upper body probes contribute heat only.
	template<typename F> void ForEachBodyProbe(F&& Probe) const
	{
		Probe(SampleHeightCm, false);
		Probe(FMath::Min(20.f, SampleHeightCm), false);
		Probe(SampleHeightCm * 0.5f, false);
		if (BodyHeatHeightCm > SampleHeightCm)
		{
			for (float Height = SampleHeightCm + FMath::Clamp(SampleStepCm, 5.f, 20.f);
				Height < BodyHeatHeightCm; Height += FMath::Clamp(SampleStepCm, 5.f, 20.f)) Probe(Height, true);
			Probe(BodyHeatHeightCm, true);
		}
	}
};

struct FYUFSHazardPathScore
{
	float Length = 0.f;
	float AddedCost = 0.f;
	float MaxSmoke = 0.f;
	float MaxHeat = 0.f;
	int32 OutsideSamples = 0;
	bool bUnsafeAhead = false;
};

struct FYUFSHazardSnapshot
{
	TSharedPtr<const FYUFSHazardGrid, ESPMode::ThreadSafe> Grid;
	FTransform GridToWorld = FTransform::Identity; // Cell corners; includes all three axes and reflection.
	EYUFSHazardDataStatus Status = EYUFSHazardDataStatus::MissingData;
	int32 Frame = INDEX_NONE;

	// When present, only personally observed cells contribute; unseen cells are unknown, not certified safe.
	TSharedPtr<const TMap<int32, FYUFSHazardSample>, ESPMode::ThreadSafe> KnownCells;
	// Includes recently observed clear cells, so expired/unseen evidence cannot masquerade as clearance.
	TSharedPtr<const TSet<int32>, ESPMode::ThreadSafe> ObservedCells;
	bool IsObservedAt(const FVector& WorldLocation) const;
	int32 CellIndex(const FVector& WorldLocation) const;
	FYUFSHazardSample Sample(const FVector& WorldLocation) const;
	float SegmentAddedCost(const FVector& Start, const FVector& End, const FYUFSHazardSettings& Settings) const;
	FYUFSHazardPathScore ScorePath(const TArray<FVector>& FloorPoints, const FYUFSHazardSettings& Settings) const;
};

