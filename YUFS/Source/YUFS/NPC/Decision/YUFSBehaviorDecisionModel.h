#pragma once

#include "CoreMinimal.h"
#include "Core/YUFSTypes.h"

struct FYUFSActionWeight
{
	FYUFSActionWeight() = default;
	FYUFSActionWeight(EYUFSAction InAction, float InWeight, bool bInAllowed = true)
		: Action(InAction), Weight(InWeight), bAllowed(bInAllowed) {}

	EYUFSAction Action = EYUFSAction::Idle;
	float Weight = 0.f;
	bool bAllowed = true;
};

struct FYUFSRouteCandidate
{
	FYUFSRouteCandidate() = default;
	FYUFSRouteCandidate(EYUFSRouteStrategy InStrategy, float InPriorWeight, float InUtility, bool bInAvailable)
		: Strategy(InStrategy), PriorWeight(InPriorWeight), Utility(InUtility), bAvailable(bInAvailable) {}

	EYUFSRouteStrategy Strategy = EYUFSRouteStrategy::None;
	float PriorWeight = 0.f;
	float Utility = 0.f;
	bool bAvailable = false;
};

/** Pure, seed-driven choices used by the NPC runtime and automation tests. */
class YUFS_API FYUFSBehaviorDecisionModel
{
public:
	static float ComputeCommitProbability(float BaseProbability, TConstArrayView<float> LikelihoodRatios);
	static int32 SelectPreEvacuationActionCount(
		FRandomStream& Stream,
		float ShortBandWeight,
		float MediumBandWeight,
		float LongBandWeight);
	static EYUFSAction SelectWeightedAction(
		FRandomStream& Stream,
		TConstArrayView<FYUFSActionWeight> Candidates,
		EYUFSAction FallbackAction = EYUFSAction::SeekInformation);
	static float SelectDuration(FRandomStream& Stream, FVector2D DurationRange);
	static EYUFSRouteStrategy SelectRoute(
		FRandomStream& Stream,
		TConstArrayView<FYUFSRouteCandidate> Candidates);
};
