// Fill out your copyright notice in the Description page of Project Settings.

#include "Core/YUFSObservation.h"

namespace
{
constexpr float DistanceNormalizeCm = 10000.f;
constexpr float CountNormalizeMax = 20.f;
constexpr float MillingNormalizeMax = 20.f;

float NormalizeDistance(float DistanceCm)
{
	return FMath::Clamp(DistanceCm / DistanceNormalizeCm, 0.f, 1.f);
}

float NormalizeCoordinate(float CoordinateCm)
{
	return FMath::Clamp(CoordinateCm / DistanceNormalizeCm, -1.f, 1.f);
}

float NormalizeCount(int32 Count)
{
	return FMath::Clamp(static_cast<float>(Count) / CountNormalizeMax, 0.f, 1.f);
}
}

void FYUFSNPCObservation::FillFloatArray(TArray<float>& OutArray) const
{
	OutArray.SetNumUninitialized(FeatureCount);

	OutArray[0] = SmokeDensityAtSelf;
	OutArray[1] = TemperatureAtSelf;
	OutArray[2] = SmokeInFrontNormalized;
	OutArray[3] = SmokeAboveNormalized;
	OutArray[4] = RiskLevel;
	OutArray[5] = SimTimeNormalized;
	OutArray[6] = NormalizeDistance(DistToNearestExit);
	OutArray[7] = NormalizeDistance(DistToFamiliarExit);
	OutArray[8] = static_cast<float>(DirToNearestExit.X);
	OutArray[9] = static_cast<float>(DirToNearestExit.Y);
	OutArray[10] = static_cast<float>(DirToNearestExit.Z);
	OutArray[11] = bNearestExitSmokeFree ? 1.0f : 0.0f;
	OutArray[12] = NearbyEvacuatingRatio;
	OutArray[13] = NormalizeCount(NearbyNPCCount);
	OutArray[14] = NormalizeCount(GroupSize);
	OutArray[15] = bNearbyNPCNeedsHelp ? 1.0f : 0.0f;
	OutArray[16] = bAlarmSounding ? 1.0f : 0.0f;
	OutArray[17] = bReceivedPreRecordedMsg ? 1.0f : 0.0f;
	OutArray[18] = bReceivedLiveAnnouncement ? 1.0f : 0.0f;
	OutArray[19] = bReceivedStaffGuidance ? 1.0f : 0.0f;
	OutArray[20] = NormalizeCoordinate(StaffGuidedExitLocation.X);
	OutArray[21] = NormalizeCoordinate(StaffGuidedExitLocation.Y);
	OutArray[22] = NormalizeCoordinate(StaffGuidedExitLocation.Z);
	OutArray[23] = static_cast<float>(CurrentState) / static_cast<float>(EYUFSBehaviorState::Incapacitated);
	OutArray[24] = RiskPerception;
	OutArray[25] = StressLevel;
	OutArray[26] = FMath::Clamp(static_cast<float>(MillingActionCount) / MillingNormalizeMax, 0.f, 1.f);
	OutArray[27] = SmokeExposureAccumulated;
}
