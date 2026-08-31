#pragma once

#include "CoreMinimal.h"

/** 서로 영향을 주지 않는 NPC 의사결정 난수 스트림. */
enum class EYUFSRngStream : uint8
{
	Decision,
	TaskDuration,
	Route,
	Social,
	Count
};

/**
 * scenarioSeed + stableNpcId + stream tag로 초기화하는 결정적 난수 묶음.
 * 한 스트림의 draw 수가 늘어나도 다른 스트림의 결과는 바뀌지 않는다.
 */
class YUFS_API FYUFSDeterministicRngSet
{
public:
	void Initialize(int32 InScenarioSeed, int32 InStableNpcId);

	bool IsInitialized() const { return bInitialized; }
	float FRand(EYUFSRngStream Stream);
	float FRandRange(EYUFSRngStream Stream, float Min, float Max);
	bool Roll(EYUFSRngStream Stream, float Probability);

	uint64 GetDrawCount(EYUFSRngStream Stream) const;
	int32 GetStreamSeed(EYUFSRngStream Stream) const;

private:
	static constexpr int32 StreamCount = static_cast<int32>(EYUFSRngStream::Count);
	static uint32 MixBits(uint32 Value);
	static int32 ToIndex(EYUFSRngStream Stream);

	FRandomStream Streams[StreamCount];
	uint64 DrawCounts[StreamCount]{};
	int32 StreamSeeds[StreamCount]{};
	bool bInitialized = false;
};
