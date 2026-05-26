// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Core/YUFSObservation.h"
#include "Core/YUFSTypes.h"

class YUFS_API FYUFSExperienceLogger
{
public:
	static void LogTransition(
		const FString& AgentId,
		int32 RunIndex,
		int32 StepIndex,
		int32 SimFrame,
		float SimTimeSeconds,
		const FYUFSNPCObservation& State,
		EYUFSAction Action,
		const FYUFSNPCObservation& NextState,
		bool bDone,
		EYUFSTerminalReason TerminalReason);

	static FString GetLogFilePath();

private:
	static bool EnsureLogFile();
	static void WriteLine(const FString& Line);
	static FString SerializeObservation(const FYUFSNPCObservation& Observation);
	static FString EscapeCsv(const FString& Value);
	static FString GetActionName(EYUFSAction Action);
	static FString GetTerminalReasonName(EYUFSTerminalReason TerminalReason);
};
