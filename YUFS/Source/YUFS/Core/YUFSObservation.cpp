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

TArray<float> FYUFSNPCObservation::ToFloatArray() const
{
	TArray<float> OutArray;
	OutArray.Reserve(29);

	OutArray.Add(SmokeDensityAtSelf);
	OutArray.Add(TemperatureAtSelf);
	OutArray.Add(SmokeInFrontNormalized);
	OutArray.Add(SmokeAboveNormalized);
	OutArray.Add(RiskLevel);
	OutArray.Add(SimTimeNormalized);

	OutArray.Add(NormalizeDistance(DistToNearestExit));
	OutArray.Add(NormalizeDistance(DistToFamiliarExit));
	OutArray.Add(static_cast<float>(DirToNearestExit.X));
	OutArray.Add(static_cast<float>(DirToNearestExit.Y));
	OutArray.Add(static_cast<float>(DirToNearestExit.Z));
	OutArray.Add(bNearestExitSmokeFree ? 1.0f : 0.0f);
	OutArray.Add(NormalizeDistance(DistToNearestShelter));

	OutArray.Add(NearbyEvacuatingRatio);
	OutArray.Add(NormalizeCount(NearbyNPCCount));
	OutArray.Add(NormalizeCount(GroupSize));
	OutArray.Add(bNearbyNPCNeedsHelp ? 1.0f : 0.0f);

	OutArray.Add(bAlarmSounding ? 1.0f : 0.0f);
	OutArray.Add(bReceivedPreRecordedMsg ? 1.0f : 0.0f);
	OutArray.Add(bReceivedLiveAnnouncement ? 1.0f : 0.0f);
	OutArray.Add(bReceivedStaffGuidance ? 1.0f : 0.0f);
	OutArray.Add(NormalizeCoordinate(StaffGuidedExitLocation.X));
	OutArray.Add(NormalizeCoordinate(StaffGuidedExitLocation.Y));
	OutArray.Add(NormalizeCoordinate(StaffGuidedExitLocation.Z));

	OutArray.Add(static_cast<float>(CurrentState) / static_cast<float>(EYUFSBehaviorState::Incapacitated));
	OutArray.Add(RiskPerception);
	OutArray.Add(StressLevel);
	OutArray.Add(FMath::Clamp(static_cast<float>(MillingActionCount) / MillingNormalizeMax, 0.f, 1.f));
	OutArray.Add(SmokeExposureAccumulated);

	return OutArray;
}
