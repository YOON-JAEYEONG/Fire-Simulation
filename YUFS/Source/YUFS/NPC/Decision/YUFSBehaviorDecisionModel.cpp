#include "NPC/Decision/YUFSBehaviorDecisionModel.h"

namespace
{
constexpr float ProbabilityEpsilon = 1.e-6f;

float PositiveFiniteOrZero(float Value)
{
	return FMath::IsFinite(Value) && Value > 0.f ? Value : 0.f;
}
}

float FYUFSBehaviorDecisionModel::ComputeCommitProbability(
	float BaseProbability,
	TConstArrayView<float> LikelihoodRatios)
{
	if (!FMath::IsFinite(BaseProbability)) return 0.f;
	if (BaseProbability <= 0.f) return 0.f;
	if (BaseProbability >= 1.f) return 1.f;

	double Odds = static_cast<double>(BaseProbability) / static_cast<double>(1.f - BaseProbability);
	for (const float Ratio : LikelihoodRatios)
	{
		if (FMath::IsFinite(Ratio) && Ratio > 0.f)
		{
			Odds *= static_cast<double>(Ratio);
		}
	}

	if (!FMath::IsFinite(Odds)) return 1.f;
	return FMath::Clamp(static_cast<float>(Odds / (1.0 + Odds)), 0.f, 1.f);
}

int32 FYUFSBehaviorDecisionModel::SelectPreEvacuationActionCount(
	FRandomStream& Stream,
	float ShortBandWeight,
	float MediumBandWeight,
	float LongBandWeight)
{
	const float ShortWeight = PositiveFiniteOrZero(ShortBandWeight);
	const float MediumWeight = PositiveFiniteOrZero(MediumBandWeight);
	const float LongWeight = PositiveFiniteOrZero(LongBandWeight);
	const float TotalWeight = ShortWeight + MediumWeight + LongWeight;
	if (TotalWeight <= ProbabilityEpsilon) return 1;

	const float Roll = Stream.GetFraction() * TotalWeight;
	if (Roll < ShortWeight) return Stream.RandRange(1, 5);
	if (Roll < ShortWeight + MediumWeight) return Stream.RandRange(6, 9);
	return Stream.RandRange(10, 15);
}

EYUFSAction FYUFSBehaviorDecisionModel::SelectWeightedAction(
	FRandomStream& Stream,
	TConstArrayView<FYUFSActionWeight> Candidates,
	EYUFSAction FallbackAction)
{
	float TotalWeight = 0.f;
	for (const FYUFSActionWeight& Candidate : Candidates)
	{
		if (Candidate.bAllowed)
		{
			TotalWeight += PositiveFiniteOrZero(Candidate.Weight);
		}
	}

	if (TotalWeight <= ProbabilityEpsilon) return FallbackAction;

	const float Roll = Stream.GetFraction() * TotalWeight;
	float AccumulatedWeight = 0.f;
	for (const FYUFSActionWeight& Candidate : Candidates)
	{
		if (!Candidate.bAllowed) continue;
		AccumulatedWeight += PositiveFiniteOrZero(Candidate.Weight);
		if (Roll <= AccumulatedWeight) return Candidate.Action;
	}

	return FallbackAction;
}

float FYUFSBehaviorDecisionModel::SelectDuration(FRandomStream& Stream, FVector2D DurationRange)
{
	const float MinDuration = FMath::Max(0.1f, FMath::Min(DurationRange.X, DurationRange.Y));
	const float MaxDuration = FMath::Max(MinDuration, FMath::Max(DurationRange.X, DurationRange.Y));
	return Stream.FRandRange(MinDuration, MaxDuration);
}

EYUFSRouteStrategy FYUFSBehaviorDecisionModel::SelectRoute(
	FRandomStream& Stream,
	TConstArrayView<FYUFSRouteCandidate> Candidates)
{
	TArray<FYUFSRouteCandidate> OrderedCandidates;
	for (const FYUFSRouteCandidate& Candidate : Candidates)
	{
		if (Candidate.bAvailable && PositiveFiniteOrZero(Candidate.PriorWeight) > 0.f)
		{
			OrderedCandidates.Add(Candidate);
		}
	}

	if (OrderedCandidates.IsEmpty()) return EYUFSRouteStrategy::ShelterInPlace;

	OrderedCandidates.Sort([](const FYUFSRouteCandidate& Left, const FYUFSRouteCandidate& Right)
	{
		return static_cast<uint8>(Left.Strategy) < static_cast<uint8>(Right.Strategy);
	});

	EYUFSRouteStrategy BestStrategy = EYUFSRouteStrategy::ShelterInPlace;
	float BestScore = -MAX_flt;
	for (const FYUFSRouteCandidate& Candidate : OrderedCandidates)
	{
		const float Uniform = FMath::Clamp(Stream.GetFraction(), ProbabilityEpsilon, 1.f - ProbabilityEpsilon);
		const float GumbelNoise = -FMath::Loge(-FMath::Loge(Uniform));
		const float Score = FMath::Loge(Candidate.PriorWeight) + Candidate.Utility + GumbelNoise;
		if (Score > BestScore)
		{
			BestScore = Score;
			BestStrategy = Candidate.Strategy;
		}
	}

	return BestStrategy;
}
