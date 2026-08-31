#include "Core/YUFSDeterministicRng.h"

namespace
{
constexpr uint32 StreamTags[] =
{
	0xA341316Cu, // Decision
	0xC8013EA4u, // TaskDuration
	0xAD90777Du, // Route
	0x7E95761Eu  // Social
};
static_assert(UE_ARRAY_COUNT(StreamTags) == static_cast<int32>(EYUFSRngStream::Count));
}

void FYUFSDeterministicRngSet::Initialize(int32 InScenarioSeed, int32 InStableNpcId)
{
	const uint32 ScenarioBits = MixBits(static_cast<uint32>(InScenarioSeed));
	const uint32 NpcBits = MixBits(static_cast<uint32>(InStableNpcId));

	for (int32 Index = 0; Index < StreamCount; ++Index)
	{
		const uint32 MixedSeed = MixBits(ScenarioBits ^ NpcBits ^ StreamTags[Index]);
		StreamSeeds[Index] = static_cast<int32>(MixedSeed & 0x7fffffffu);
		Streams[Index].Initialize(StreamSeeds[Index]);
		DrawCounts[Index] = 0;
	}

	bInitialized = true;
}

float FYUFSDeterministicRngSet::FRand(EYUFSRngStream Stream)
{
	const int32 Index = ToIndex(Stream);
	checkf(bInitialized, TEXT("FYUFSDeterministicRngSet must be initialized before use."));
	++DrawCounts[Index];
	return Streams[Index].FRand();
}

float FYUFSDeterministicRngSet::FRandRange(EYUFSRngStream Stream, float Min, float Max)
{
	return FMath::Lerp(Min, Max, FRand(Stream));
}

bool FYUFSDeterministicRngSet::Roll(EYUFSRngStream Stream, float Probability)
{
	return FRand(Stream) < FMath::Clamp(Probability, 0.f, 1.f);
}

uint64 FYUFSDeterministicRngSet::GetDrawCount(EYUFSRngStream Stream) const
{
	return DrawCounts[ToIndex(Stream)];
}

int32 FYUFSDeterministicRngSet::GetStreamSeed(EYUFSRngStream Stream) const
{
	return StreamSeeds[ToIndex(Stream)];
}

uint32 FYUFSDeterministicRngSet::MixBits(uint32 Value)
{
	Value ^= Value >> 16;
	Value *= 0x7feb352du;
	Value ^= Value >> 15;
	Value *= 0x846ca68bu;
	Value ^= Value >> 16;
	return Value;
}

int32 FYUFSDeterministicRngSet::ToIndex(EYUFSRngStream Stream)
{
	const int32 Index = static_cast<int32>(Stream);
	check(Index >= 0 && Index < StreamCount);
	return Index;
}
