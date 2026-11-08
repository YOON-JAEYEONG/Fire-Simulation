// Fill out your copyright notice in the Description page of Project Settings.

#include "Core/YUFSExperienceLogger.h"

#include "HAL/CriticalSection.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/Archive.h"

namespace
{
FCriticalSection LogMutex;
TUniquePtr<FArchive> LogArchive;
FString LogFilePath;
TArray<float> ObservationScratchBuffer;
int32 LinesSinceFlush = 0;
constexpr int32 FlushIntervalLines = 256;

FString FormatFloat(float Value)
{
	return FString::SanitizeFloat(Value);
}
}

void FYUFSExperienceLogger::LogTransition(
	const FString& AgentId,
	int32 RunIndex,
	int32 StepIndex,
	int32 SimFrame,
	float SimTimeSeconds,
	const FYUFSNPCObservation& State,
	EYUFSAction Action,
	const FYUFSNPCObservation& NextState,
	bool bDone,
	EYUFSTerminalReason TerminalReason)
{
	FScopeLock Lock(&LogMutex);
	if (!EnsureLogFile())
	{
		return;
	}

	const FString Line = FString::Printf(
		TEXT("%d,%s,%d,%d,%s,%s,%s,%s,%s,%s\n"),
		RunIndex,
		*EscapeCsv(AgentId),
		StepIndex,
		SimFrame,
		*FormatFloat(SimTimeSeconds),
		*EscapeCsv(GetActionName(Action)),
		bDone ? TEXT("1") : TEXT("0"),
		*EscapeCsv(GetTerminalReasonName(TerminalReason)),
		*EscapeCsv(SerializeObservation(State)),
		*EscapeCsv(SerializeObservation(NextState)));

	WriteLine(Line);
}

FString FYUFSExperienceLogger::GetLogFilePath()
{
	FScopeLock Lock(&LogMutex);
	EnsureLogFile();
	return LogFilePath;
}

bool FYUFSExperienceLogger::EnsureLogFile()
{
	if (LogArchive.IsValid())
	{
		return true;
	}

	const FString LogDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Transitions"));
	IFileManager::Get().MakeDirectory(*LogDirectory, true);

	const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	LogFilePath = FPaths::Combine(LogDirectory, FString::Printf(TEXT("yufs_transitions_%s.csv"), *Timestamp));

	LogArchive.Reset(IFileManager::Get().CreateFileWriter(*LogFilePath, FILEWRITE_AllowRead));
	if (!LogArchive.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[YUFS] Failed to create transition log: %s"), *LogFilePath);
		return false;
	}

	WriteLine(TEXT("run_id,agent_id,step_index,sim_frame,sim_time_seconds,action,done,terminal_reason,state,next_state\n"));
	UE_LOG(LogTemp, Log, TEXT("[YUFS] Writing transition log: %s"), *LogFilePath);
	return true;
}

void FYUFSExperienceLogger::WriteLine(const FString& Line)
{
	if (!LogArchive.IsValid())
	{
		return;
	}

	FTCHARToUTF8 Converter(*Line);
	LogArchive->Serialize(reinterpret_cast<void*>(const_cast<ANSICHAR*>(Converter.Get())), Converter.Length());
	if (++LinesSinceFlush >= FlushIntervalLines)
	{
		LogArchive->Flush();
		LinesSinceFlush = 0;
	}
}

FString FYUFSExperienceLogger::SerializeObservation(const FYUFSNPCObservation& Observation)
{
	Observation.FillFloatArray(ObservationScratchBuffer);
	FString Result;
	Result.Reserve(ObservationScratchBuffer.Num() * 12);

	for (int32 Index = 0; Index < ObservationScratchBuffer.Num(); ++Index)
	{
		if (Index > 0)
		{
			Result.AppendChar(TEXT(';'));
		}
		Result.Append(FormatFloat(ObservationScratchBuffer[Index]));
	}

	return Result;
}

FString FYUFSExperienceLogger::EscapeCsv(const FString& Value)
{
	FString Escaped = Value;
	Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));
	return FString::Printf(TEXT("\"%s\""), *Escaped);
}

FString FYUFSExperienceLogger::GetActionName(EYUFSAction Action)
{
	if (const UEnum* ActionEnum = StaticEnum<EYUFSAction>())
	{
		return ActionEnum->GetNameStringByValue(static_cast<int64>(Action));
	}

	return FString::FromInt(static_cast<int32>(Action));
}

FString FYUFSExperienceLogger::GetTerminalReasonName(EYUFSTerminalReason TerminalReason)
{
	if (const UEnum* TerminalEnum = StaticEnum<EYUFSTerminalReason>())
	{
		return TerminalEnum->GetNameStringByValue(static_cast<int64>(TerminalReason));
	}

	return FString::FromInt(static_cast<int32>(TerminalReason));
}
