#include "NPC/Cognition/YUFSHumanCognitionComponent.h"

#include "Core/YUFSDeterministicRng.h"
#include "Core/YUFSObservation.h"
#include "NPC/Cognition/YUFSBehaviorPolicy.h"

namespace
{
constexpr uint32 EvidenceAlarm = 1u << 0;
constexpr uint32 EvidenceSmoke = 1u << 1;
constexpr uint32 EvidenceLifeRisk = 1u << 2;
constexpr uint32 EvidenceRecordedMessage = 1u << 3;
constexpr uint32 EvidenceLiveMessage = 1u << 4;
constexpr uint32 EvidenceStaffGuidance = 1u << 5;
constexpr uint32 EvidenceMovingCrowd = 1u << 6;
constexpr uint32 EvidenceStationaryCrowd = 1u << 7;
constexpr uint32 EvidenceHelpRequest = 1u << 8;
}

UYUFSHumanCognitionComponent::UYUFSHumanCognitionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UYUFSHumanCognitionComponent::Initialize(
	int32 StableNpcId,
	FYUFSDeterministicRngSet& RandomSource)
{
	if (PolicyAsset)
	{
		Traits = PolicyAsset->DefaultTraits;
	}

	if (bGenerateDeterministicTraitVariation)
	{
		const float Range = FMath::Clamp(TraitVariationHalfRange, 0.f, 0.5f);
		auto Jitter = [&RandomSource, Range](float Value)
		{
			return FMath::Clamp(
				Value + RandomSource.FRandRange(EYUFSRngStream::Traits, -Range, Range),
				0.f,
				1.f);
		};

		Traits.BuildingFamiliarity = Jitter(Traits.BuildingFamiliarity);
		Traits.FireTraining = Jitter(Traits.FireTraining);
		Traits.PhysicalMobility = Jitter(Traits.PhysicalMobility);
		Traits.StressSensitivity = Jitter(Traits.StressSensitivity);
		Traits.RiskTolerance = Jitter(Traits.RiskTolerance);
		Traits.AuthorityTrust = Jitter(Traits.AuthorityTrust);
		Traits.SocialConformity = Jitter(Traits.SocialConformity);
		Traits.HelpingTendency = Jitter(Traits.HelpingTendency);
		Traits.GroupAttachment = Jitter(Traits.GroupAttachment);
		Traits.HabitStrength = Jitter(Traits.HabitStrength);
	}

	CognitiveState = FYUFSCognitiveState{};
	CognitiveState.NormalcyBias = Traits.HabitStrength;
	CognitiveState.LastEvidenceTrigger = TEXT("Initialized");
	LastEvidenceSignature = MAX_uint32;
	PreviousSeverity = EYUFSPerceivedPhysicalSeverity::None;
	bInitialized = true;
	(void)StableNpcId;
}

void UYUFSHumanCognitionComponent::UpdateCognition(
	float DeltaTime,
	const FYUFSNPCObservation& Observation)
{
	if (!bInitialized)
	{
		// Unit tests and Blueprint-created components remain deterministic even when
		// the owning NPC has not supplied a trait stream yet.
		bInitialized = true;
		CognitiveState.NormalcyBias = Traits.HabitStrength;
	}

	const EYUFSPerceivedPhysicalSeverity Severity = ResolvePhysicalSeverity(Observation);
	const uint32 NewSignature = BuildEvidenceSignature(Observation, Severity);
	bEvidenceChanged = NewSignature != LastEvidenceSignature;
	const bool bNewFreezeCue = bEvidenceChanged
		&& static_cast<uint8>(Severity) > static_cast<uint8>(PreviousSeverity)
		&& static_cast<uint8>(Severity) >= static_cast<uint8>(EYUFSPerceivedPhysicalSeverity::ConfirmedSmoke);
	bFreezeCueThisUpdate = bFreezeCueThisUpdate || bNewFreezeCue;

	if (bEvidenceChanged)
	{
		CognitiveState.LastEvidenceTrigger = ResolveEvidenceTrigger(LastEvidenceSignature, NewSignature);
		++CognitiveState.EvidenceRevision;
		LastEvidenceSignature = NewSignature;
	}

	const float DirectHazard = FMath::Max3(
		FMath::Clamp(Observation.SmokeDensityAtSelf, 0.f, 1.f),
		FMath::Clamp(Observation.TemperatureAtSelf, 0.f, 1.f),
		FMath::Clamp(Observation.RiskLevel, 0.f, 1.f));
	const bool bOfficial = Observation.bReceivedStaffGuidance || Observation.bReceivedLiveAnnouncement;
	const bool bMovingCrowd = Observation.NearbyEvacuatingRatio >= 0.30f;
	const bool bStationaryCrowd = Observation.NearbyNPCCount >= 3
		&& Observation.NearbyEvacuatingRatio <= 0.10f;

	float CueRisk = 0.f;
	switch (Severity)
	{
	case EYUFSPerceivedPhysicalSeverity::AmbiguousAlarm: CueRisk = 0.25f; break;
	case EYUFSPerceivedPhysicalSeverity::ConfirmedSmoke: CueRisk = 0.65f; break;
	case EYUFSPerceivedPhysicalSeverity::ImmediateLifeThreat: CueRisk = 0.95f; break;
	default: break;
	}
	if (bOfficial)
	{
		CueRisk = FMath::Max(CueRisk, 0.90f * Traits.AuthorityTrust);
	}
	if (bMovingCrowd)
	{
		CueRisk = FMath::Max(CueRisk, 0.45f * Traits.SocialConformity);
	}

	float NormalcyTarget = Traits.HabitStrength * (1.f - DirectHazard);
	if (Observation.bAlarmSounding)
	{
		NormalcyTarget *= 0.75f;
	}
	if (bStationaryCrowd)
	{
		NormalcyTarget = FMath::Clamp(
			NormalcyTarget + 0.20f * Traits.SocialConformity,
			0.f,
			1.f);
	}
	if (bOfficial || Severity == EYUFSPerceivedPhysicalSeverity::ImmediateLifeThreat)
	{
		NormalcyTarget = 0.f;
	}

	const float ConfidenceTarget = FMath::Clamp(
		FMath::Max(CueRisk, DirectHazard) + (bOfficial ? 0.20f : 0.f),
		0.f,
		1.f);
	const float PerceivedRiskTarget = FMath::Clamp(
		FMath::Max(CueRisk, DirectHazard) - NormalcyTarget * 0.20f,
		0.f,
		1.f);
	const float UrgencyTarget = FMath::Clamp(
		PerceivedRiskTarget * (1.15f - 0.30f * Traits.RiskTolerance),
		0.f,
		1.f);
	const float StressTarget = FMath::Clamp(
		PerceivedRiskTarget * (0.55f + 0.65f * Traits.StressSensitivity)
			+ (Observation.NearbyNPCCount >= 5 ? 0.10f : 0.f),
		0.f,
		1.f);
	const float CognitiveLoadTarget = FMath::Clamp(
		FMath::Max(0.f, StressTarget - 0.55f) * 1.5f
			+ FMath::Clamp(Observation.SmokeInFrontNormalized, 0.f, 1.f) * 0.35f,
		0.f,
		1.f);

	const float ResponseRate = PolicyAsset ? PolicyAsset->CognitionResponseRate : 2.f;
	CognitiveState.PhysicalSeverity = Severity;
	CognitiveState.NormalcyBias = MoveToward(CognitiveState.NormalcyBias, NormalcyTarget, ResponseRate, DeltaTime);
	CognitiveState.SituationConfidence = MoveToward(CognitiveState.SituationConfidence, ConfidenceTarget, ResponseRate, DeltaTime);
	CognitiveState.PerceivedRisk = MoveToward(CognitiveState.PerceivedRisk, PerceivedRiskTarget, ResponseRate, DeltaTime);
	CognitiveState.Urgency = MoveToward(CognitiveState.Urgency, UrgencyTarget, ResponseRate, DeltaTime);
	CognitiveState.Stress = MoveToward(CognitiveState.Stress, StressTarget, ResponseRate, DeltaTime);
	CognitiveState.CognitiveLoad = MoveToward(CognitiveState.CognitiveLoad, CognitiveLoadTarget, ResponseRate, DeltaTime);
	CognitiveState.CurrentPlanCommitment = FMath::Clamp(
		CognitiveState.CurrentPlanCommitment + DeltaTime * 0.05f,
		0.f,
		1.f);

	PreviousSeverity = Severity;
}

void UYUFSHumanCognitionComponent::NotifyPlanChanged()
{
	CognitiveState.CurrentPlanCommitment = 0.f;
}

bool UYUFSHumanCognitionComponent::ConsumeFreezeCue()
{
	const bool bResult = bFreezeCueThisUpdate;
	bFreezeCueThisUpdate = false;
	return bResult;
}

float UYUFSHumanCognitionComponent::MoveToward(
	float Current,
	float Target,
	float Rate,
	float DeltaTime)
{
	const float Alpha = 1.f - FMath::Exp(-FMath::Max(Rate, 0.01f) * FMath::Max(DeltaTime, 0.f));
	return FMath::Lerp(Current, Target, Alpha);
}

uint32 UYUFSHumanCognitionComponent::BuildEvidenceSignature(
	const FYUFSNPCObservation& Observation,
	EYUFSPerceivedPhysicalSeverity Severity) const
{
	uint32 Signature = 0;
	if (Observation.bAlarmSounding) Signature |= EvidenceAlarm;
	if (static_cast<uint8>(Severity) >= static_cast<uint8>(EYUFSPerceivedPhysicalSeverity::ConfirmedSmoke)) Signature |= EvidenceSmoke;
	if (Severity == EYUFSPerceivedPhysicalSeverity::ImmediateLifeThreat) Signature |= EvidenceLifeRisk;
	if (Observation.bReceivedPreRecordedMsg) Signature |= EvidenceRecordedMessage;
	if (Observation.bReceivedLiveAnnouncement) Signature |= EvidenceLiveMessage;
	if (Observation.bReceivedStaffGuidance) Signature |= EvidenceStaffGuidance;
	if (Observation.NearbyEvacuatingRatio >= 0.30f) Signature |= EvidenceMovingCrowd;
	if (Observation.NearbyNPCCount >= 3 && Observation.NearbyEvacuatingRatio <= 0.10f) Signature |= EvidenceStationaryCrowd;
	if (Observation.bNearbyNPCNeedsHelp) Signature |= EvidenceHelpRequest;
	return Signature;
}

EYUFSPerceivedPhysicalSeverity UYUFSHumanCognitionComponent::ResolvePhysicalSeverity(
	const FYUFSNPCObservation& Observation) const
{
	const float SmokeThreshold = PolicyAsset ? PolicyAsset->ConfirmedSmokeThreshold : 0.15f;
	const float LifeSmokeThreshold = PolicyAsset ? PolicyAsset->LifeRiskSmokeThreshold : 0.70f;
	const float LifeTemperatureThreshold = PolicyAsset ? PolicyAsset->LifeRiskTemperatureThreshold : 0.80f;

	if (Observation.SmokeDensityAtSelf >= LifeSmokeThreshold
		|| Observation.TemperatureAtSelf >= LifeTemperatureThreshold)
	{
		return EYUFSPerceivedPhysicalSeverity::ImmediateLifeThreat;
	}
	if (Observation.SmokeDensityAtSelf >= SmokeThreshold
		|| Observation.SmokeInFrontNormalized >= SmokeThreshold
		|| Observation.SmokeAboveNormalized >= SmokeThreshold)
	{
		return EYUFSPerceivedPhysicalSeverity::ConfirmedSmoke;
	}
	if (Observation.bAlarmSounding)
	{
		return EYUFSPerceivedPhysicalSeverity::AmbiguousAlarm;
	}
	return EYUFSPerceivedPhysicalSeverity::None;
}

FName UYUFSHumanCognitionComponent::ResolveEvidenceTrigger(
	uint32 PreviousSignature,
	uint32 NewSignature) const
{
	if (PreviousSignature == MAX_uint32) return TEXT("InitialSnapshot");
	const uint32 Added = NewSignature & ~PreviousSignature;
	if ((Added & EvidenceLifeRisk) != 0) return TEXT("LifeRiskObserved");
	if ((Added & EvidenceSmoke) != 0) return TEXT("SmokeObserved");
	if ((Added & EvidenceStaffGuidance) != 0) return TEXT("StaffGuidanceReceived");
	if ((Added & EvidenceLiveMessage) != 0) return TEXT("LiveAnnouncementReceived");
	if ((Added & EvidenceAlarm) != 0) return TEXT("AlarmReceived");
	if ((Added & EvidenceMovingCrowd) != 0) return TEXT("CrowdStartedEvacuating");
	if ((Added & EvidenceStationaryCrowd) != 0) return TEXT("StationaryCrowdObserved");
	if ((Added & EvidenceHelpRequest) != 0) return TEXT("HelpNeedObserved");
	return TEXT("EvidenceChanged");
}
