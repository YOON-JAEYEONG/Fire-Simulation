// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Core/YUFSDecisionPolicy.h"
#include "Core/YUFSDeterministicRng.h"
#include "Core/YUFSObservation.h"
#include "Core/YUFSTypes.h"

/**
 *
 */
class YUFS_API FYUFSRuleBasedPolicy : public IYUFSDecisionPolicy
{
public:
	virtual EYUFSAction SelectAction(const FYUFSNPCObservation& Obs) override;
	void SetRandomSource(FYUFSDeterministicRngSet* InRandomSource) { RandomSource = InRandomSource; }

private:
	bool Roll(EYUFSRngStream Stream, float Probability);
	FYUFSDeterministicRngSet* RandomSource = nullptr;
};
