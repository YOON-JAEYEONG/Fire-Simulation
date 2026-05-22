// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Core/YUFSTypes.h"
#include "YUFSDecisionPolicy.generated.h"

struct FYUFSNPCObservation;
// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class UYUFSDecisionPolicy : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class YUFS_API IYUFSDecisionPolicy
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
	// ── 공통 인터페이스 (Rule-Based·RL 모두 구현 필수) ────────────────
	virtual EYUFSAction SelectAction(const FYUFSNPCObservation& Obs) = 0;

	// ── RL 전용 인터페이스 (Rule-Based는 빈 구현) ─────────────────────
	// 한 스텝이 끝난 후 다음 상태·보상·종료 여부를 전달
	virtual void OnTransition(
		const FYUFSNPCObservation& PrevObs,
		EYUFSAction Action,
		const FYUFSNPCObservation& NextObs,
		float Reward,
		bool bDone) {}

	virtual bool IsLearningMode() const { return false; }

	// 학습된 모델 저장/불러오기 (RL 전용)
	virtual void SaveModel(const FString& Path) {}
	virtual void LoadModel(const FString& Path) {}
};
