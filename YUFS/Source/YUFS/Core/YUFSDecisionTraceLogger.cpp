#include "Core/YUFSDecisionTraceLogger.h"

#include "HAL/CriticalSection.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/Archive.h"

namespace
{
FCriticalSection DecisionTraceMutex;
TUniquePtr<FArchive> DecisionTraceArchive;
FString DecisionTracePath;
int32 DecisionLinesSinceFlush = 0;
constexpr int32 DecisionFlushInterval = 64;
}

void FYUFSDecisionTraceLogger::LogEvent(
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
	uint64 SocialDrawCount)
{
	FScopeLock Lock(&DecisionTraceMutex);
	if (!EnsureLogFile())
	{
		return;
	}

	const FString Line = FString::Printf(
		TEXT("{\"runId\":%d,\"npcId\":%d,\"tick\":%d,\"simTime\":%s,")
		TEXT("\"policyHash\":\"%s\",\"scenarioHash\":\"%s\",\"decisionIndex\":%s,")
		TEXT("\"fromIntent\":\"%s\",\"toIntent\":\"%s\",\"trigger\":\"%s\",")
		TEXT("\"pCommit\":%s,\"cueMask\":%u,\"hasSafeExit\":%s,")
		TEXT("\"task\":\"%s\",\"cancelReason\":\"%s\",")
		TEXT("\"preActionCompleted\":%d,\"preActionTarget\":%d,")
		TEXT("\"rngDraws\":{\"decision\":%s,\"duration\":%s,\"route\":%s,\"social\":%s},")
		TEXT("\"sourceIds\":[\"B:initial\",\"D:p8-10\",\"E:p23-26\",\"PROJECT:decision-draft\"]}\n"),
		RunIndex,
		StableNpcId,
		SimFrame,
		*FString::SanitizeFloat(SimTimeSeconds),
		*EscapeJson(PolicyHash),
		*EscapeJson(ScenarioHash),
		*LexToString(DecisionIndex),
		*EscapeJson(GetIntentName(FromIntent)),
		*EscapeJson(GetIntentName(ToIntent)),
		*EscapeJson(Trigger),
		*FString::SanitizeFloat(CommitProbability),
		CueMask,
		bHasSafeExit ? TEXT("true") : TEXT("false"),
		*EscapeJson(GetTaskName(Task)),
		*EscapeJson(GetCancelReasonName(CancelReason)),
		PreActionCompletedCount,
		PreActionTargetCount,
		*LexToString(DecisionDrawCount),
		*LexToString(DurationDrawCount),
		*LexToString(RouteDrawCount),
		*LexToString(SocialDrawCount));

	WriteLine(Line);
}

FString FYUFSDecisionTraceLogger::GetLogFilePath()
{
	FScopeLock Lock(&DecisionTraceMutex);
	EnsureLogFile();
	return DecisionTracePath;
}

bool FYUFSDecisionTraceLogger::EnsureLogFile()
{
	if (DecisionTraceArchive.IsValid())
	{
		return true;
	}

	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DecisionTraces"));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	DecisionTracePath = FPaths::Combine(Directory, FString::Printf(TEXT("yufs_decisions_%s.jsonl"), *Timestamp));
	DecisionTraceArchive.Reset(IFileManager::Get().CreateFileWriter(*DecisionTracePath, FILEWRITE_AllowRead));
	if (!DecisionTraceArchive.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[YUFS] Failed to create decision trace: %s"), *DecisionTracePath);
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("[YUFS] Writing decision trace: %s"), *DecisionTracePath);
	return true;
}

void FYUFSDecisionTraceLogger::WriteLine(const FString& Line)
{
	FTCHARToUTF8 Converter(*Line);
	DecisionTraceArchive->Serialize(
		reinterpret_cast<void*>(const_cast<ANSICHAR*>(Converter.Get())),
		Converter.Length());
	if (++DecisionLinesSinceFlush >= DecisionFlushInterval)
	{
		DecisionTraceArchive->Flush();
		DecisionLinesSinceFlush = 0;
	}
}

FString FYUFSDecisionTraceLogger::EscapeJson(const FString& Value)
{
	FString Escaped = Value;
	Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
	Escaped.ReplaceInline(TEXT("\r"), TEXT("\\r"));
	Escaped.ReplaceInline(TEXT("\n"), TEXT("\\n"));
	return Escaped;
}

FString FYUFSDecisionTraceLogger::GetIntentName(EYUFSIntent Intent)
{
	const UEnum* Enum = StaticEnum<EYUFSIntent>();
	return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Intent)) : FString::FromInt(static_cast<int32>(Intent));
}

FString FYUFSDecisionTraceLogger::GetTaskName(EYUFSActionTask Task)
{
	const UEnum* Enum = StaticEnum<EYUFSActionTask>();
	return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Task)) : FString::FromInt(static_cast<int32>(Task));
}

FString FYUFSDecisionTraceLogger::GetCancelReasonName(EYUFSTaskCancelReason Reason)
{
	const UEnum* Enum = StaticEnum<EYUFSTaskCancelReason>();
	return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Reason)) : FString::FromInt(static_cast<int32>(Reason));
}
