#include "Fire/YUFSHazardField.h"

FYUFSHazardSample FYUFSHazardSnapshot::Sample(const FVector& WorldLocation) const
{
	FYUFSHazardSample Result;
	Result.Status = Status;
	if (Status != EYUFSHazardDataStatus::Ready) return Result;
	if (!Grid.IsValid())
	{
		Result.Status = EYUFSHazardDataStatus::MissingData;
		return Result;
	}
	if (WorldLocation.ContainsNaN() || !GridToWorld.IsValid() ||
		GridToWorld.GetScale3D().GetAbsMin() <= SMALL_NUMBER)
	{
		Result.Status = EYUFSHazardDataStatus::InvalidMapping;
		return Result;
	}
	const FVector Cell = GridToWorld.InverseTransformPosition(WorldLocation);
	const FIntVector& D = Grid->Dimensions;
	// Bounds check before converting to int, including negative coordinates.
	if (Cell.X < 0 || Cell.Y < 0 || Cell.Z < 0 || Cell.X >= D.X || Cell.Y >= D.Y || Cell.Z >= D.Z)
	{
		Result.Status = EYUFSHazardDataStatus::OutsideDomain;
		return Result;
	}
	const int32 Index = (FMath::FloorToInt(Cell.X) * D.Y + FMath::FloorToInt(Cell.Y)) * D.Z + FMath::FloorToInt(Cell.Z);
	if (!Grid->Density.IsValidIndex(Index) || !Grid->Temperature.IsValidIndex(Index))
	{
		Result.Status = EYUFSHazardDataStatus::MissingData;
		return Result;
	}
	if (KnownCells.IsValid())
	{
		if (const auto* Known = KnownCells->Find(Index)) return *Known;
		return Result;
	}
	Result.Smoke = Grid->Density[Index] / 255.f;
	Result.Heat = Grid->Temperature[Index] / 255.f;
	return Result;
}

namespace
{
	FYUFSHazardSample SampleBody(const FYUFSHazardSnapshot& Field, const FVector& Feet, float Height)
	{
		auto Value = Field.Sample(Feet + FVector(0, 0, Height));
		// Ground fire and smoke at breathing height both matter.
		for (const float Z : {FMath::Min(20.f, Height), Height * 0.5f})
		{
			const auto Other = Field.Sample(Feet + FVector(0, 0, Z));
			Value.Smoke = FMath::Max(Value.Smoke, Other.Smoke);
			Value.Heat = FMath::Max(Value.Heat, Other.Heat);
			if (Other.Status == EYUFSHazardDataStatus::Ready) Value.Status = Other.Status;
		}
		return Value;
	}
	int32 SampleCount(const FVector& A, const FVector& B, const FYUFSHazardSettings& Settings)
	{
		return FMath::Clamp(FMath::CeilToInt(FVector::Distance(A, B) / FMath::Max(5.f, Settings.SampleStepCm)), 1, 4096);
	}
	float Penalty(const FYUFSHazardSample& Sample, const FYUFSHazardSettings& Settings)
	{
		return FMath::Max(0.f, Settings.SmokeCost) * FMath::Square(Sample.Smoke)
			+ FMath::Max(0.f, Settings.HeatCost) * FMath::Square(Sample.Heat);
	}
}

float FYUFSHazardSnapshot::SegmentAddedCost(const FVector& Start, const FVector& End,
	const FYUFSHazardSettings& Settings) const
{
	const int32 Count = SampleCount(Start, End, Settings);
	float Sum = 0.f;
	for (int32 I = 0; I < Count; ++I)
	{
		const auto Value = SampleBody(*this, FMath::Lerp(Start, End, (I + 0.5f) / Count), Settings.SampleHeightCm);
		Sum += Penalty(Value, Settings);
	}
	return FVector::Distance(Start, End) * Sum / Count;
}

FYUFSHazardPathScore FYUFSHazardSnapshot::ScorePath(const TArray<FVector>& FloorPoints,
	const FYUFSHazardSettings& Settings) const
{
	FYUFSHazardPathScore Score;
	bool bFirst = true;
	bool bEscaping = false;
	float InitialSmoke = 0.f, InitialHeat = 0.f;
	for (int32 Segment = 1; Segment < FloorPoints.Num(); ++Segment)
	{
		const FVector& A = FloorPoints[Segment - 1];
		const FVector& B = FloorPoints[Segment];
		Score.Length += FVector::Distance(A, B);
		Score.AddedCost += SegmentAddedCost(A, B, Settings);
		const int32 Count = SampleCount(A, B, Settings);
		for (int32 I = 0; I <= Count; ++I)
		{
			const auto Value = SampleBody(*this, FMath::Lerp(A, B, static_cast<float>(I) / Count), Settings.SampleHeightCm);
			Score.OutsideSamples += Value.Status == EYUFSHazardDataStatus::OutsideDomain ? 1 : 0;
			Score.MaxSmoke = FMath::Max(Score.MaxSmoke, Value.Smoke);
			Score.MaxHeat = FMath::Max(Score.MaxHeat, Value.Heat);
			const bool bDanger = Value.Smoke >= Settings.BlockSmoke || Value.Heat >= Settings.BlockHeat;
			if (bFirst)
			{
				bEscaping = bDanger;
				InitialSmoke = Value.Smoke;
				InitialHeat = Value.Heat;
				bFirst = false;
			}
			if (bEscaping && !bDanger) bEscaping = false;
			// Allow leaving an already dangerous location, but not entering a worse pocket or re-entering danger.
			if (bDanger && (!bEscaping || Value.Smoke > InitialSmoke + 0.05f || Value.Heat > InitialHeat + 0.05f))
				Score.bUnsafeAhead = true;
		}
	}
	return Score;
}

int32 FYUFSHazardSnapshot::CellIndex(const FVector& P) const
{
	if (!Grid || P.ContainsNaN() || !GridToWorld.IsValid() || GridToWorld.GetScale3D().GetAbsMin() <= SMALL_NUMBER) return INDEX_NONE;
	const FVector C = GridToWorld.InverseTransformPosition(P);
	const auto D = Grid->Dimensions;
	if (C.X < 0 || C.Y < 0 || C.Z < 0 || C.X >= D.X || C.Y >= D.Y || C.Z >= D.Z) return INDEX_NONE;
	return (FMath::FloorToInt(C.X) * D.Y + FMath::FloorToInt(C.Y)) * D.Z + FMath::FloorToInt(C.Z);
}
