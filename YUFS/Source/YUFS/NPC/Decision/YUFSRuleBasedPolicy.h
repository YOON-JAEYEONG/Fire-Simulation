// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Core/YUFSDecisionPolicy.h"
#include "Core/YUFSObservation.h"
#include "Core/YUFSTypes.h"

/**
 * 
 */
class YUFS_API FYUFSRuleBasedPolicy : public IYUFSDecisionPolicy
{
public:
	virtual EYUFSAction SelectAction(const FYUFSNPCObservation& Obs) override
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
		case EYUFSBehaviorState::Milling:
			// 논문(Latane&Darley): 주변 30% 이상 대피 중이면 동조
			if (Obs.NearbyEvacuatingRatio > 0.3f) return EYUFSAction::AlertNearbyOccupants;
			return EYUFSAction::SeekInformation;

		case EYUFSBehaviorState::Preparing:
			if (Obs.StressLevel < 0.5f) return EYUFSAction::GatherBelongings;
			return EYUFSAction::AlertNearbyOccupants;

		case EYUFSBehaviorState::Evacuating:
			// 군중 휩쓸리기 (Herd Instinct)
			if (Obs.NearbyNPCCount > 2 && Obs.NearbyEvacuatingRatio >= 0.5f)
			{
				// 70% 확률로 주변 군중의 평균 목적지로 따라감
				if (FMath::FRand() < 0.7f)
				{
					return EYUFSAction::FollowCrowd;
				}
			}

			// 논문(Sime, Affiliative): 친숙한 출구 선호
			if (Obs.DistToFamiliarExit < Obs.DistToNearestExit * 1.5f
				&& Obs.bNearestExitSmokeFree)
				return EYUFSAction::EvacuateToFamiliarExit;
			return EYUFSAction::EvacuateToNearestExit;

		case EYUFSBehaviorState::Helping:
			return EYUFSAction::HelpOther;

		case EYUFSBehaviorState::Sheltering:
			return EYUFSAction::MoveToShelter;

		default:
			return EYUFSAction::Idle;
		}
	}
};