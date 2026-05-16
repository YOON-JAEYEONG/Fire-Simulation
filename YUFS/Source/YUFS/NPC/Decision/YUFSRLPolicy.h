// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Core/YUFSDecisionPolicy.h"
#include "Core/YUFSObservation.h"

/**
 * 
 */
class YUFS_API FYUFSRLPolicy : public IYUFSDecisionPolicy
{
public:
	virtual EYUFSAction SelectAction(const FYUFSNPCObservation& Obs) override
	{
		// TODO: FYUFSNPCObservation → float[] 직렬화 → ONNX 신경망 추론
		TArray<float> StateVec = Obs.ToFloatArray();
		// return InferFromNetwork(StateVec);
		return EYUFSAction::Idle; // 현재는 빈 구현
	}

	virtual void OnTransition(const FYUFSNPCObservation& NextObs,
							   float Reward, bool bDone) override
	{
		// TODO: Experience Replay Buffer에 (S, A, R, S', done) 저장
	}

	virtual bool IsLearningMode() const override { return true; }
};
