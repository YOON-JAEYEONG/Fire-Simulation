// Fill out your copyright notice in the Description page of Project Settings.


#include "Core/YUFSRewardCalculator.h"

YUFSRewardCalculator::YUFSRewardCalculator()
{
}

YUFSRewardCalculator::~YUFSRewardCalculator()
{
}

float YUFSRewardCalculator::Calculate(
	const FYUFSNPCObservation& PrevObs,
	EYUFSAction TakenAction,
	const FYUFSNPCObservation& NextObs,
	EYUFSTerminalReason TerminalReason)
{
	float Reward = 0.f;

	// +10: 출구 도달 (생존)
	if (TerminalReason == EYUFSTerminalReason::ReachedExit)
	{
		Reward += 10.f;
	}
	else if (TerminalReason == EYUFSTerminalReason::Incapacitated)
	{
		Reward -= 10.f;
	}
	else if (TerminalReason == EYUFSTerminalReason::TimedOut)
	{
		Reward -= 3.f;
	}

	// -0.01/step: 매 프레임 생존 비용 (빠른 대피 유도)
	Reward -= 0.01f;

	// -연기 노출 패널티
	Reward -= NextObs.SmokeDensityAtSelf * 0.5f;
	
	// -온도 노출 패널티
	Reward -= NextObs.TemperatureAtSelf * 0.2f;
	Reward -= NextObs.SmokeExposureAccumulated * 0.3f;

	// +타인 도움 보상 (Altruistic Behavior 장려)
	if (TakenAction == EYUFSAction::HelpOther) Reward += 0.3f;

	// -촬영 행동 페널티 (van der Wal: 대피 지연의 핵심 원인)
	if (TakenAction == EYUFSAction::Film) Reward -= 0.15f;

	// -불필요한 지연 패널티 (Milling 과다)
	if (NextObs.MillingActionCount > 10) Reward -= 0.1f;
	
	// +목표 출구 방향으로의 진행 보상 — 행동에 맞는 거리 기준 사용
	// EvacuateToFamiliarExit: 친숙 출구 기준, MoveToShelter: 대피처 기준, 나머지: 최근접 출구 기준
	float DistDiff;
	if (TakenAction == EYUFSAction::EvacuateToFamiliarExit)
	{
		DistDiff = PrevObs.DistToFamiliarExit - NextObs.DistToFamiliarExit;
	}
	else if (TakenAction == EYUFSAction::MoveToShelter)
	{
		DistDiff = PrevObs.DistToNearestShelter - NextObs.DistToNearestShelter;
	}
	else
	{
		DistDiff = PrevObs.DistToNearestExit - NextObs.DistToNearestExit;
	}
	const float DistDiffMeters = DistDiff / 100.f;
	if (DistDiff > 0.f)
	{
		Reward += DistDiffMeters * 0.2f;
	}
	else if (DistDiff < 0.f)
	{
		// 목표 방향에서 멀어지면 약간의 패널티
		Reward += DistDiffMeters * 0.1f;
	}

	return Reward;
}
