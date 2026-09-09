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
	CueAnnouncement = 1u << 6,
	CueStationaryCrowd = 1u << 7
};
}

UYUFSBeliefComponent::UYUFSBeliefComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UYUFSBeliefComponent::SetCognitiveContext(
	float NormalcyBias,
	float SocialConformity,
	float AuthorityTrust)
{
	CognitiveNormalcyBias = FMath::Clamp(NormalcyBias, 0.f, 1.f);
	CognitiveSocialConformity = FMath::Clamp(SocialConformity, 0.f, 1.f);
	CognitiveAuthorityTrust = FMath::Clamp(AuthorityTrust, 0.f, 1.f);
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
	const bool bStationaryCrowd = Observation.NearbyNPCCount >= 3
		&& Observation.NearbyEvacuatingRatio <= 0.10f;

	float BaseProbability = NoCueBaseProbability;
	PhysicalSeverity = EYUFSPerceivedPhysicalSeverity::None;
	if (Observation.bAlarmSounding)
	{
		ActiveCueMask |= CueAlarm;
		BaseProbability = FMath::Max(BaseProbability, AlarmBaseProbability);
		PhysicalSeverity = EYUFSPerceivedPhysicalSeverity::AmbiguousAlarm;
	}
	if (bConfirmedSmoke)
	{
		ActiveCueMask |= CueSmoke;
		BaseProbability = FMath::Max(BaseProbability, ConfirmedSmokeBaseProbability);
		PhysicalSeverity = EYUFSPerceivedPhysicalSeverity::ConfirmedSmoke;
	}
	if (bHighHeat)
	{
		ActiveCueMask |= CueHighHeat;
		BaseProbability = FMath::Max(BaseProbability, HighHeatBaseProbability);
		PhysicalSeverity = EYUFSPerceivedPhysicalSeverity::ImmediateLifeThreat;
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
	if (bStationaryCrowd)
	{
		ActiveCueMask |= CueStationaryCrowd;
	}
	if (bTrainingCompleted)
	{
		ActiveCueMask |= CueTraining;
	}

	bHasEmergencyCue = (ActiveCueMask & ~(CueTraining | CueStationaryCrowd)) != 0;
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
		Odds *= FMath::Pow(
			FMath::Sqrt(FMath::Max(LeaderLikelihoodRatio, 0.01f)),
			CognitiveAuthorityTrust);
	}
	if (bMovingCrowd)
	{
		Odds *= FMath::Pow(
			FMath::Max(MovingCrowdLikelihoodRatio, 0.01f),
			CognitiveSocialConformity);
	}
	if (bStationaryCrowd && Observation.bAlarmSounding)
	{
		Odds *= FMath::Pow(
			FMath::Max(StationaryCrowdLikelihoodRatio, 0.01f),
			CognitiveSocialConformity);
	}
	const float NormalcyExponent = CognitiveNormalcyBias
		* (PhysicalSeverity == EYUFSPerceivedPhysicalSeverity::ImmediateLifeThreat ? 0.f : 1.f);
	Odds *= FMath::Pow(FMath::Max(NormalcyLikelihoodRatio, 0.01f), NormalcyExponent);

	CommitProbability = FMath::Clamp(
		Odds / (1.f + Odds),
		FMath::Min(MinimumCommitProbability, MaximumCommitProbability),
		FMath::Max(MinimumCommitProbability, MaximumCommitProbability));
}

FString UYUFSBeliefComponent::GetPolicyHash() const
{
	const FString Canonical = FString::Printf(
		TEXT("belief|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%d"),
		NoCueBaseProbability,
		AlarmBaseProbability,
		ConfirmedSmokeBaseProbability,
		HighHeatBaseProbability,
		TrainingLikelihoodRatio,
		LeaderLikelihoodRatio,
		MovingCrowdLikelihoodRatio,
		StationaryCrowdLikelihoodRatio,
		NormalcyLikelihoodRatio,
		MinimumCommitProbability,
		MaximumCommitProbability,
		ConfirmedSmokeThreshold,
		ImmediateLifeRiskSmokeThreshold,
		ImmediateLifeRiskTemperatureThreshold,
		bTrainingCompleted ? 1 : 0);
	return FString::Printf(TEXT("crc32:%08x"), FCrc::StrCrc32(*Canonical));
}
