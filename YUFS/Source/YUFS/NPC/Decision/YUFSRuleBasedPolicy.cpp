// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/Decision/YUFSRuleBasedPolicy.h"

EYUFSAction FYUFSRuleBasedPolicy::SelectAction(const FYUFSNPCObservation& Obs)
{
	// 우선순위 1: 스태프 안내 — 논문상 가장 효과적 (Odds Ratio 0.33)
	if (Obs.bReceivedStaffGuidance)
		return EYUFSAction::EvacuateToNearestExit;

	// 우선순위 2: 행동 불능 판정
	if (Obs.CurrentState == EYUFSBehaviorState::Incapacitated)
		return EYUFSAction::Cough;

	// 우선순위 3: 상태별 행동
	switch (Obs.CurrentState)
	{
	// 단서 인지 중 및 위험 판단 중: 주변 정보를 적극적으로 수집
	case EYUFSBehaviorState::Perceiving:
	case EYUFSBehaviorState::RiskAssessment:
		return EYUFSAction::SeekInformation;

	case EYUFSBehaviorState::Milling:
		// 논문(van der Wal): 경보 존재 시 촬영 OR 3.43 (p=0.04, 유일한 유의 예측변수)
		// 사전 녹음 메시지를 받으면 공식 안내 인지로 촬영 동기 감소 → Film 확률 제거
		if (Obs.bAlarmSounding && !Obs.bReceivedPreRecordedMsg && FMath::FRand() < 0.28f)
		{
			return EYUFSAction::Film;
		}
		// 논문(Latane&Darley): 주변 30% 이상 대피 중이면 동조
		if (Obs.NearbyEvacuatingRatio > 0.3f) return EYUFSAction::AlertNearbyOccupants;
		// 사전 녹음 메시지 수신 시 안내 내용을 주변에 전파 (정보 확산 행동)
		if (Obs.bReceivedPreRecordedMsg) return EYUFSAction::AlertNearbyOccupants;
		return EYUFSAction::SeekInformation;

	case EYUFSBehaviorState::Preparing:
		if (Obs.StressLevel < 0.5f) return EYUFSAction::GatherBelongings;
		return EYUFSAction::AlertNearbyOccupants;

	case EYUFSBehaviorState::Evacuating:
		// 구체적인 경로 행동은 NPC에 한 번 배정된 70:20:10 RoutePreference 계층에서
		// 결정한다. 여기서 매 정책 Tick마다 확률을 다시 굴리면 집단 비율과 재현성이 깨진다.
		return EYUFSAction::EvacuateToNearestExit;

	case EYUFSBehaviorState::Helping:
		return EYUFSAction::HelpOther;

	case EYUFSBehaviorState::Crawling:
		// 기어가면서도 가장 가까운 출구로 이동 (이동속도는 상태머신이 CrawlSpeed로 제한)
		return EYUFSAction::EvacuateToNearestExit;

	default:
		return EYUFSAction::Idle;
	}
}
