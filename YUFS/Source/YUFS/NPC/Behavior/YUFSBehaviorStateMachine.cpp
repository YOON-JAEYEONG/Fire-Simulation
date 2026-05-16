// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "Core/YUFSObservation.h"

UYUFSBehaviorStateMachine::UYUFSBehaviorStateMachine()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UYUFSBehaviorStateMachine::BeginPlay()
{
	Super::BeginPlay();
}

void UYUFSBehaviorStateMachine::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UYUFSBehaviorStateMachine::TickStateMachine(float DeltaTime, const FYUFSNPCObservation& Obs)
{
	if (!Config)
	{
		return;
	}

	const EYUFSBehaviorState PrevState = CurrentState;
	StateTimer += DeltaTime;

	AccumulateRiskPerception(Obs, DeltaTime);
	AccumulateSmokeExposure(Obs, DeltaTime);
	TryTransition(Obs);

	if (PrevState != CurrentState)
	{
		StateTimer = 0.f;
	}
}

void UYUFSBehaviorStateMachine::AccumulateRiskPerception(const FYUFSNPCObservation& Obs, float DeltaTime)
{
	if (!Config)
	{
		return;
	}

	float RiskIncrease = 0.f;

	if (Obs.SmokeDensityAtSelf > Config->SmokeAwarenessThreshold)
	{
		RiskIncrease += Config->RiskAccumSpeed * (Obs.SmokeDensityAtSelf / Config->SmokeAwarenessThreshold);
	}

	if (Obs.TemperatureAtSelf > 0.1f)
	{
		RiskIncrease += Config->RiskAccumSpeed * (Obs.TemperatureAtSelf * 10.f);
	}

	if (Obs.SmokeInFrontNormalized > Config->VisionSmokeCueThreshold)
	{
		const float FrontSmokeNormalized =
			(Obs.SmokeInFrontNormalized - Config->VisionSmokeCueThreshold) /
			FMath::Max(KINDA_SMALL_NUMBER, 1.f - Config->VisionSmokeCueThreshold);
		RiskIncrease += Config->RiskAccumSpeed * Config->VisionRiskAccumMultiplier *
			FMath::Clamp(FrontSmokeNormalized, 0.f, 1.f);
	}

	if (Obs.SmokeAboveNormalized > Config->AboveSmokeCueThreshold)
	{
		const float AboveSmokeNormalized =
			(Obs.SmokeAboveNormalized - Config->AboveSmokeCueThreshold) /
			FMath::Max(KINDA_SMALL_NUMBER, 1.f - Config->AboveSmokeCueThreshold);
		RiskIncrease += Config->RiskAccumSpeed * Config->AboveSmokeRiskAccumMultiplier *
			FMath::Clamp(AboveSmokeNormalized, 0.f, 1.f);
	}

	if (Obs.NearbyEvacuatingRatio > 0.3f)
	{
		RiskIncrease += Config->RiskAccumSpeed;
	}
	else if (Obs.NearbyNPCCount > 2 && Obs.NearbyEvacuatingRatio < 0.1f)
	{
		RiskIncrease -= Config->RiskAccumSpeed * 0.8f;
	}

	if (RiskIncrease == 0.f && !Obs.bAlarmSounding && !Obs.bReceivedStaffGuidance)
	{
		RiskIncrease -= Config->RiskAccumSpeed * 0.5f;
	}

	RiskPerception = FMath::Clamp(RiskPerception + (RiskIncrease * DeltaTime), 0.f, 1.f);
}

void UYUFSBehaviorStateMachine::TryTransition(const FYUFSNPCObservation& Obs)
{
	if (!Config)
	{
		return;
	}

	// 행동불능 전이 조건: 뒤에 아무 상태에서든 누적 흡입량이 임계값 초과 시 즉시 Incapacitated
	if (CurrentState != EYUFSBehaviorState::Incapacitated && Config)
	{
		if (SmokeExposureAccumulated >= Config->IncapacitationThreshold)
		{
			CurrentState = EYUFSBehaviorState::Incapacitated;
			return;
		}
	}

	if (CheckEmergencyOverride(Obs))
	{
		CurrentState = EYUFSBehaviorState::Evacuating;
		return;
	}

	switch (CurrentState)
	{
	case EYUFSBehaviorState::Normal:
		if (Obs.SmokeDensityAtSelf > Config->SmokeAwarenessThreshold ||
			Obs.SmokeInFrontNormalized > Config->VisionSmokeCueThreshold ||
			Obs.SmokeAboveNormalized > Config->AboveSmokeCueThreshold ||
			Obs.bAlarmSounding ||
			Obs.bReceivedLiveAnnouncement ||
			Obs.bReceivedStaffGuidance)
		{
			CurrentState = EYUFSBehaviorState::Perceiving;
		}
		break;

	case EYUFSBehaviorState::Perceiving:
		if (StateTimer > 2.f)
		{
			CurrentState = EYUFSBehaviorState::Milling;
		}
		break;

	case EYUFSBehaviorState::Milling:
		if (Obs.bReceivedStaffGuidance ||
			RiskPerception > Config->RiskPerceptionThreshold ||
			StateTimer > Config->MaxMillingDuration)
		{
			CurrentState = EYUFSBehaviorState::RiskAssessment;
		}
		break;

	case EYUFSBehaviorState::RiskAssessment:
		if (RiskPerception > Config->RiskPerceptionThreshold)
		{
			CurrentState = EYUFSBehaviorState::Preparing;
		}
		else if (RiskPerception < Config->RiskPerceptionThreshold * 0.5f)
		{
			CurrentState = EYUFSBehaviorState::Milling;
		}
		break;

	case EYUFSBehaviorState::Preparing:
		if (StateTimer > Config->PreparationDuration || Obs.bReceivedStaffGuidance)
		{
			CurrentState = EYUFSBehaviorState::Evacuating;
		}
		break;

	case EYUFSBehaviorState::Evacuating:
	default:
		break;
	}
}

bool UYUFSBehaviorStateMachine::CheckEmergencyOverride(const FYUFSNPCObservation& Obs) const
{
	if (!Config)
	{
		return false;
	}

	return Obs.SmokeDensityAtSelf > (Config->SmokeAwarenessThreshold * Config->EmergencyOverrideMultiplier);
}

void UYUFSBehaviorStateMachine::AccumulateSmokeExposure(const FYUFSNPCObservation& Obs, float DeltaTime)
{
	if (!Config) return;

	// 연기가 있을 때만 누적. 연기 농도에 비례하여 더 빠르게 누적.
	if (Obs.SmokeDensityAtSelf > 0.f)
	{
		const float ExposureRate = Config->SmokeExposureAccumRate * Obs.SmokeDensityAtSelf;
		SmokeExposureAccumulated = FMath::Clamp(SmokeExposureAccumulated + ExposureRate * DeltaTime, 0.f, 1.f);
	}
	// 연기가 없는 곳에서는 아주 쳌쳌 히복 (신선한 공기 찼는 중)
	else
	{
		const float RecoveryRate = Config->SmokeExposureAccumRate * 0.1f;
		SmokeExposureAccumulated = FMath::Clamp(SmokeExposureAccumulated - RecoveryRate * DeltaTime, 0.f, 1.f);
	}
}

void UYUFSBehaviorStateMachine::OnAlarmReceived()
{
	RiskPerception = FMath::Clamp(RiskPerception + 0.1f, 0.f, 1.f);
}

void UYUFSBehaviorStateMachine::OnStaffGuidanceReceived()
{
	RiskPerception = FMath::Clamp(RiskPerception + 0.5f, 0.f, 1.f);
}

void UYUFSBehaviorStateMachine::OnLiveAnnouncementReceived()
{
	RiskPerception = FMath::Clamp(RiskPerception + 0.3f, 0.f, 1.f);
}
