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

	// 알람 울리는 동안 지속적으로 위험 인식 증가 (van der Wal: 알람이 가장 강한 행동 촉발 변수)
	if (Obs.bAlarmSounding)
	{
		RiskIncrease += Config->RiskAccumSpeed * 0.8f;
	}

	if (Obs.NearbyEvacuatingRatio > 0.3f)
	{
		RiskIncrease += Config->RiskAccumSpeed;
	}
	else if (Obs.NearbyNPCCount > 2 && Obs.NearbyEvacuatingRatio < 0.1f && !Obs.bAlarmSounding)
	{
		// 알람 없을 때만 군중 비활성으로 인한 감소 적용
		RiskIncrease -= Config->RiskAccumSpeed * 0.8f;
	}

	// 알람·스태프 안내가 없고 별다른 자극도 없을 때만 자연 감소
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

	// 우선순위 1: 행동불능 — 모든 상태에서 최우선 적용
	if (CurrentState != EYUFSBehaviorState::Incapacitated
		&& SmokeExposureAccumulated >= Config->IncapacitationThreshold)
	{
		CurrentState = EYUFSBehaviorState::Incapacitated;
		return;
	}

	// 우선순위 2: 기어가기 진입 — 비기어가기 상태에서 CrawlThreshold 초과 시
	if (CurrentState != EYUFSBehaviorState::Incapacitated
		&& CurrentState != EYUFSBehaviorState::Crawling
		&& SmokeExposureAccumulated >= Config->CrawlThreshold)
	{
		CurrentState = EYUFSBehaviorState::Crawling;
		return;
	}

	// 우선순위 3: 긴급 오버라이드 — Crawling/Incapacitated는 신체 상태 기반이므로 제외
	if (CurrentState != EYUFSBehaviorState::Evacuating
		&& CurrentState != EYUFSBehaviorState::Crawling
		&& CheckEmergencyOverride(Obs))
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
			Obs.bReceivedPreRecordedMsg ||
			Obs.bReceivedLiveAnnouncement ||
			Obs.bReceivedStaffGuidance)
		{
			CurrentState = EYUFSBehaviorState::Perceiving;
		}
		break;

	case EYUFSBehaviorState::Perceiving:
		if (StateTimer > Config->PerceivinDuration)
		{
			CurrentState = EYUFSBehaviorState::Milling;
		}
		break;

	case EYUFSBehaviorState::Milling:
		if (Obs.bReceivedStaffGuidance || RiskPerception > Config->RiskPerceptionThreshold)
		{
			// 위험 인식이 충분하면 즉시 위험 판단 단계로
			CurrentState = EYUFSBehaviorState::RiskAssessment;
		}
		else if (StateTimer > Config->MaxMillingDuration)
		{
			// 충분한 시간 동안 탐색했으면 Preparing으로 직행 — RiskAssessment 루프 방지
			// (PADM: Milling 종료 후 결정을 내리는 것이 현실적 행동)
			CurrentState = EYUFSBehaviorState::Preparing;
		}
		break;

	case EYUFSBehaviorState::RiskAssessment:
		if (RiskPerception > Config->RiskPerceptionThreshold || Obs.bAlarmSounding)
		{
			// 알람이 울리고 있으면 위험 수치와 무관하게 대피 준비
			CurrentState = EYUFSBehaviorState::Preparing;
		}
		else if (RiskPerception < Config->RiskPerceptionThreshold * 0.5f && !Obs.bAlarmSounding)
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

	case EYUFSBehaviorState::Crawling:
		// 신선한 공기 구역에서 충분히 회복되면 Evacuating으로 복귀
		if (SmokeExposureAccumulated < Config->CrawlThreshold)
		{
			CurrentState = EYUFSBehaviorState::Evacuating;
		}
		break;

	case EYUFSBehaviorState::Helping:
		// 주변 NPC가 더 이상 도움이 필요 없거나 최대 도움 시간 초과 시 대피 재개
		if (!Obs.bNearbyNPCNeedsHelp || StateTimer > Config->MaxHelpingDuration)
		{
			CurrentState = EYUFSBehaviorState::Evacuating;
		}
		break;

	case EYUFSBehaviorState::Sheltering:
		// 위험 인식이 충분히 낮아지고 연기도 해소되면 대피 재개
		if (RiskPerception < Config->RiskPerceptionThreshold * 0.3f
			&& Obs.SmokeDensityAtSelf < Config->SmokeAwarenessThreshold)
		{
			CurrentState = EYUFSBehaviorState::Evacuating;
		}
		break;

	case EYUFSBehaviorState::Evacuating:
		// Helping 진입: 주변 NPC가 도움을 필요로 하고 위험 인식이 충분히 낮을 때
		// (Behavioral fact #6 — 사람들은 화재 시 이타적으로 행동)
		if (Obs.bNearbyNPCNeedsHelp && RiskPerception < Config->RiskPerceptionThreshold * 0.7f)
		{
			CurrentState = EYUFSBehaviorState::Helping;
		}
		// Sheltering 진입: 현재 위치는 연기가 없으나 전방 경로가 강한 연기로 차단된 상황
		// — 대피 불가 판단, 안전 지점에서 대기 (Convergence Cluster)
		else if (Obs.SmokeDensityAtSelf < Config->SmokeAwarenessThreshold
			&& Obs.SmokeInFrontNormalized > Config->VisionSmokeCueThreshold * 2.f)
		{
			CurrentState = EYUFSBehaviorState::Sheltering;
		}
		break;

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

void UYUFSBehaviorStateMachine::OnPreRecordedMessageReceived()
{
	// 알람(+0.1)보다 명확한 정보를 제공하므로 더 큰 위험 인식 증가
	// 실시간 안내(+0.3)보다는 낮음 — 사전 녹음이라 상황 맞춤성이 부족하기 때문
	RiskPerception = FMath::Clamp(RiskPerception + 0.2f, 0.f, 1.f);
}

void UYUFSBehaviorStateMachine::OnStaffGuidanceReceived()
{
	RiskPerception = FMath::Clamp(RiskPerception + 0.5f, 0.f, 1.f);
}

void UYUFSBehaviorStateMachine::OnLiveAnnouncementReceived()
{
	RiskPerception = FMath::Clamp(RiskPerception + 0.3f, 0.f, 1.f);
}
