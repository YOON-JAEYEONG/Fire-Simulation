// Fill out your copyright notice in the Description page of Project Settings.

#include "Core/YUFSObservation.h"

TArray<float> FYUFSNPCObservation::ToFloatArray() const
{
	TArray<float> OutArray;

	OutArray.Add(SmokeDensityAtSelf);
	OutArray.Add(TemperatureAtSelf);
	OutArray.Add(SmokeInFrontNormalized);
	OutArray.Add(SmokeAboveNormalized);
	OutArray.Add(RiskLevel);
	OutArray.Add(SimTimeNormalized);

	OutArray.Add(DistToNearestExit);
	OutArray.Add(DistToFamiliarExit);
	OutArray.Add(static_cast<float>(DirToNearestExit.X));
	OutArray.Add(static_cast<float>(DirToNearestExit.Y));
	OutArray.Add(static_cast<float>(DirToNearestExit.Z));
	OutArray.Add(bNearestExitSmokeFree ? 1.0f : 0.0f);
	OutArray.Add(DistToNearestShelter);

	OutArray.Add(NearbyEvacuatingRatio);
	OutArray.Add(static_cast<float>(GroupSize));
	OutArray.Add(bNearbyNPCNeedsHelp ? 1.0f : 0.0f);

	OutArray.Add(bAlarmSounding ? 1.0f : 0.0f);
	OutArray.Add(bReceivedPreRecordedMsg ? 1.0f : 0.0f);
	OutArray.Add(bReceivedLiveAnnouncement ? 1.0f : 0.0f);
	OutArray.Add(bReceivedStaffGuidance ? 1.0f : 0.0f);
	OutArray.Add(static_cast<float>(StaffGuidedExitLocation.X));
	OutArray.Add(static_cast<float>(StaffGuidedExitLocation.Y));
	OutArray.Add(static_cast<float>(StaffGuidedExitLocation.Z));

	OutArray.Add(static_cast<float>(CurrentState));
	OutArray.Add(RiskPerception);
	OutArray.Add(StressLevel);
	OutArray.Add(static_cast<float>(MillingActionCount));

	return OutArray;
}
