#pragma once

#include "CoreMinimal.h"
#include "Core/YUFSTypes.h"

/** 근거 기반 의도/작업 전이를 JSONL로 기록한다. */
class YUFS_API FYUFSDecisionTraceLogger
{
public:
	static void LogEvent(
		int32 RunIndex,
		int32 StableNpcId,
		int32 SimFrame,
		float SimTimeSeconds,
		const FString& PolicyHash,
		const FString& ScenarioHash,
		uint64 DecisionIndex,
		EYUFSIntent FromIntent,
		EYUFSIntent ToIntent,
		const FString& Trigger,
		float CommitProbability,
		uint32 CueMask,
		bool bHasSafeExit,
		EYUFSActionTask Task,
		EYUFSTaskCancelReason CancelReason,
		int32 PreActionCompletedCount,
		int32 PreActionTargetCount,
		uint64 DecisionDrawCount,
		uint64 DurationDrawCount,
		uint64 RouteDrawCount,
		uint64 SocialDrawCount);

	static FString GetLogFilePath();

private:
	static bool EnsureLogFile();
	static void WriteLine(const FString& Line);
	static FString EscapeJson(const FString& Value);
	static FString GetIntentName(EYUFSIntent Intent);
	static FString GetTaskName(EYUFSActionTask Task);
	static FString GetCancelReasonName(EYUFSTaskCancelReason Reason);
};
