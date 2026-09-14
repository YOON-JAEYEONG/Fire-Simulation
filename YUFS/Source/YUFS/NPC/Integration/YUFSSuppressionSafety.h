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
	static bool ImmediateDanger(const FYUFSNPCObservation& O,
		float SmokeEmergencyThreshold = 0.30f, float HeatEmergencyThreshold = 0.65f)
	{
		// Same strict smoke / inclusive heat comparison as JJW. Values are byte-normalized,
		// NOT Celsius. Runtime callers supply the authoritative StateMachine Config limits.
		return O.SmokeDensityAtSelf > SmokeEmergencyThreshold
			|| FMath::Max3(O.TemperatureAtSelf, O.NearbyHeat, O.NearbyHeatNormalized) >= HeatEmergencyThreshold;
	}
	static bool CanAttempt(const FYUFSNPCObservation& O, const FYUFSCognitiveState& C,
		const FYUFSHumanTraits& Traits, bool bAlreadyAttempting)
	{
		return O.bSuppressionAllowedByBehavior
			&& (O.CurrentState == EYUFSBehaviorState::Evacuating || O.CurrentState == EYUFSBehaviorState::Helping)
			&& !O.bReceivedLiveAnnouncement && !O.bReceivedStaffGuidance
			// Do not apply a second hard-coded emergency threshold over JJW's configurable gate.
			&& C.PhysicalSeverity != EYUFSPerceivedPhysicalSeverity::ImmediateLifeThreat
			&& PerceivedRisk(O, C) < (bAlreadyAttempting ? StopRisk(Traits) : StartRisk(Traits));
	}
};
