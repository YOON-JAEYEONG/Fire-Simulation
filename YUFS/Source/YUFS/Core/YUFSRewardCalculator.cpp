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
	bool bReachedExit)
{
	float Reward = 0.f;

	// +10: 출구 도달 (생존)
	if (bReachedExit) Reward += 10.f;

	// -0.01/step: 매 프레임 생존 비용 (빠른 대피 유도)
	Reward -= 0.01f;

	// -연기 노출 패널티
	Reward -= NextObs.SmokeDensityAtSelf * 0.5f;
	
	// -온도 노출 패널티
	Reward -= NextObs.TemperatureAtSelf * 0.2f;

	// +타인 도움 보상 (Altruistic Behavior 장려)
	if (TakenAction == EYUFSAction::HelpOther) Reward += 0.3f;

	// -불필요한 지연 패널티 (Milling 과다)
	if (NextObs.MillingActionCount > 10) Reward -= 0.1f;
	
	// +출구 방향으로의 진행 보상 (거리 단축 시)
	float DistDiff = PrevObs.DistToNearestExit - NextObs.DistToNearestExit;
	if (DistDiff > 0.f)
	{
		Reward += DistDiff * 0.2f;
	}
	else if (DistDiff < 0.f)
	{
		// 출구에서 멀어지면 약간의 패널티
		Reward += DistDiff * 0.1f; 
	}

	return Reward;
}
