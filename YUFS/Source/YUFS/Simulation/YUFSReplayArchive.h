#pragma once

#include "CoreMinimal.h"
#include "Simulation/YUFSTimelineTypes.h"

namespace YUFSReplayArchive
{
	bool Save(const TArray<FYUFSTimelineFrame>& Frames, FString& OutRecordingId);
	bool Load(const FString& RecordingId, TArray<FYUFSTimelineFrame>& OutFrames);
	bool Exists(const FString& RecordingId);
}
