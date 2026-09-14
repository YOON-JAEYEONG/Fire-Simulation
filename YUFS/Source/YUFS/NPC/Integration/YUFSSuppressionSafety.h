#pragma once

#include "CoreMinimal.h"
#include "Core/YUFSObservation.h"
#include "NPC/Cognition/YUFSHumanCognitionTypes.h"

/** Scenario design thresholds, not measured human probabilities. No random reroll per tick. */
struct FYUFSSuppressionSafety
{
	static float StopRisk(const FYUFSHumanTraits& Traits)
	{
		return FMath::Clamp(0.55f + 0.15f * Traits.RiskTolerance
			+ 0.08f * Traits.FireTraining - 0.10f * Traits.StressSensitivity, 0.50f, 0.72f);
	}
	static float StartRisk(const FYUFSHumanTraits& Traits) { return StopRisk(Traits) - 0.10f; }
	static float PerceivedRisk(const FYUFSNPCObservation& O, const FYUFSCognitiveState& C)
	{
		return FMath::Clamp(FMath::Max3(C.PerceivedRisk, O.RiskPerception, O.RiskLevel), 0.f, 1.f);
	}
	static bool ImmediateDanger(const FYUFSNPCObservation& O)
	{
		// Byte-normalized heat, NOT Celsius. Matches reference commit's near-heat emergency gate.
		return O.SmokeDensityAtSelf >= 0.70f || O.TemperatureAtSelf >= 0.65f
			|| O.NearbyHeatNormalized >= 0.65f;
	}
	static bool CanAttempt(const FYUFSNPCObservation& O, const FYUFSCognitiveState& C,
		const FYUFSHumanTraits& Traits, bool bAlreadyAttempting)
	{
		return O.CurrentState != EYUFSBehaviorState::Incapacitated
			&& O.CurrentState != EYUFSBehaviorState::Crawling
			&& !O.bReceivedLiveAnnouncement && !O.bReceivedStaffGuidance
			&& !ImmediateDanger(O) && C.PhysicalSeverity != EYUFSPerceivedPhysicalSeverity::ImmediateLifeThreat
			&& PerceivedRisk(O, C) < (bAlreadyAttempting ? StopRisk(Traits) : StartRisk(Traits));
	}
};
