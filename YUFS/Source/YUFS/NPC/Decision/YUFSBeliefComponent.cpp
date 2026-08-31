#include "NPC/Decision/YUFSBeliefComponent.h"

#include "Core/YUFSObservation.h"
#include "Misc/Crc.h"

namespace
{
enum EYUFSCueBits : uint32
{
	CueAlarm = 1u << 0,
	CueSmoke = 1u << 1,
	CueHighHeat = 1u << 2,
	CueOfficial = 1u << 3,
	CueMovingCrowd = 1u << 4,
	CueTraining = 1u << 5,
	CueAnnouncement = 1u << 6
};
}

UYUFSBeliefComponent::UYUFSBeliefComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UYUFSBeliefComponent::UpdateBelief(const FYUFSNPCObservation& Observation)
{
	ActiveCueMask = 0;
	bVerifiedOfficialInstruction = Observation.bReceivedStaffGuidance || Observation.bReceivedLiveAnnouncement;

	const bool bConfirmedSmoke =
		Observation.SmokeDensityAtSelf >= ConfirmedSmokeThreshold ||
		Observation.SmokeInFrontNormalized >= ConfirmedSmokeThreshold ||
		Observation.SmokeAboveNormalized >= ConfirmedSmokeThreshold;
	const bool bHighHeat =
		Observation.SmokeDensityAtSelf >= ImmediateLifeRiskSmokeThreshold ||
		Observation.TemperatureAtSelf >= ImmediateLifeRiskTemperatureThreshold;
	const bool bMovingCrowd = Observation.NearbyEvacuatingRatio >= 0.30f;

	float BaseProbability = NoCueBaseProbability;
	if (Observation.bAlarmSounding)
	{
		ActiveCueMask |= CueAlarm;
		BaseProbability = FMath::Max(BaseProbability, AlarmBaseProbability);
	}
	if (bConfirmedSmoke)
	{
		ActiveCueMask |= CueSmoke;
		BaseProbability = FMath::Max(BaseProbability, ConfirmedSmokeBaseProbability);
	}
	if (bHighHeat)
	{
		ActiveCueMask |= CueHighHeat;
		BaseProbability = FMath::Max(BaseProbability, HighHeatBaseProbability);
	}
	if (bVerifiedOfficialInstruction)
	{
		ActiveCueMask |= CueOfficial;
	}
	if (Observation.bReceivedPreRecordedMsg)
	{
		ActiveCueMask |= CueAnnouncement;
	}
	if (bMovingCrowd)
	{
		ActiveCueMask |= CueMovingCrowd;
	}
	if (bTrainingCompleted)
	{
		ActiveCueMask |= CueTraining;
	}

	bHasEmergencyCue = (ActiveCueMask & ~CueTraining) != 0;
	bImmediateLifeRisk = bHighHeat;
	if (bVerifiedOfficialInstruction)
	{
		// 공식 지시는 확률 gate를 우회하지만 로그/디버그를 위해 유한값을 보존한다.
		CommitProbability = 0.999f;
		return;
	}

	const float P = FMath::Clamp(BaseProbability, 0.001f, 0.999f);
	float Odds = P / (1.f - P);
	if (bTrainingCompleted)
	{
		Odds *= FMath::Max(TrainingLikelihoodRatio, 0.01f);
	}
	if (Observation.bReceivedPreRecordedMsg)
	{
		// 사전 방송은 개인 리더가 아니므로 leader LR의 절반만 보수적으로 적용한다.
		Odds *= FMath::Sqrt(FMath::Max(LeaderLikelihoodRatio, 0.01f));
	}
	if (bMovingCrowd)
	{
		Odds *= FMath::Max(MovingCrowdLikelihoodRatio, 0.01f);
	}

	CommitProbability = FMath::Clamp(Odds / (1.f + Odds), 0.001f, 0.999f);
}

FString UYUFSBeliefComponent::GetPolicyHash() const
{
	const FString Canonical = FString::Printf(
		TEXT("belief|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%d"),
		NoCueBaseProbability,
		AlarmBaseProbability,
		ConfirmedSmokeBaseProbability,
		HighHeatBaseProbability,
		TrainingLikelihoodRatio,
		LeaderLikelihoodRatio,
		MovingCrowdLikelihoodRatio,
		ConfirmedSmokeThreshold,
		ImmediateLifeRiskSmokeThreshold,
		ImmediateLifeRiskTemperatureThreshold,
		bTrainingCompleted ? 1 : 0);
	return FString::Printf(TEXT("crc32:%08x"), FCrc::StrCrc32(*Canonical));
}
