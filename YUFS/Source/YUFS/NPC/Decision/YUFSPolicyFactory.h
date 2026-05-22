// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "YUFSRLPolicy.h"
#include "YUFSRuleBasedPolicy.h"
#include "Core/YUFSDecisionPolicy.h"

/**
 * 
 */
UENUM()
enum class EPolicyType : uint8
{
	RuleBased UMETA(DisplayName="Rule-Based"),
	RL        UMETA(DisplayName="Reinforcement Learning") // 미래
};

class FYUFSPolicyFactory
{
public:
	static TSharedPtr<IYUFSDecisionPolicy> Create(
		EPolicyType Type,
		const FString& ModelPath = FString(),
		const FString& RuntimeName = TEXT("NNERuntimeORTCpu"))
	{
		switch (Type)
		{
		case EPolicyType::RL:        
			return MakeShared<FYUFSRLPolicy>(ModelPath, RuntimeName);
		case EPolicyType::RuleBased:
		default:                     
			return MakeShared<FYUFSRuleBasedPolicy>();
		}
	}
};
