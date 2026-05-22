// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "YUFSTypes.h"
#include "YUFSObservation.h"


/**
 * 
 */
class YUFS_API YUFSRewardCalculator
{
public:
	YUFSRewardCalculator();
	~YUFSRewardCalculator();
	
	static float Calculate(
		const FYUFSNPCObservation& PrevObs,
		EYUFSAction TakenAction,
		const FYUFSNPCObservation& NextObs,
		EYUFSTerminalReason TerminalReason);
};
