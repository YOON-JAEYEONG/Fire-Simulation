#include "Core/YUFSRouteAssignment.h"

FYUFSRoutePreferenceCounts FYUFSRouteAssignment::CalculateCounts(
	int32 TotalNPCCount,
	float FamiliarRatio,
	float SocialRatio,
	float NearestRatio)
{
	FYUFSRoutePreferenceCounts Result;
	if (TotalNPCCount <= 0)
	{
		return Result;
	}

	FamiliarRatio = FMath::Max(0.f, FamiliarRatio);
	SocialRatio = FMath::Max(0.f, SocialRatio);
	NearestRatio = FMath::Max(0.f, NearestRatio);

	float RatioSum = FamiliarRatio + SocialRatio + NearestRatio;
	if (RatioSum <= KINDA_SMALL_NUMBER)
	{
		FamiliarRatio = 0.70f;
		SocialRatio = 0.20f;
		NearestRatio = 0.10f;
		RatioSum = 1.f;
	}

	const float ExactCounts[3] =
	{
		TotalNPCCount * (FamiliarRatio / RatioSum),
		TotalNPCCount * (SocialRatio / RatioSum),
		TotalNPCCount * (NearestRatio / RatioSum)
	};

	int32 Counts[3] =
	{
		FMath::FloorToInt(ExactCounts[0]),
		FMath::FloorToInt(ExactCounts[1]),
		FMath::FloorToInt(ExactCounts[2])
	};

	float FractionalRemainders[3] =
	{
		ExactCounts[0] - Counts[0],
		ExactCounts[1] - Counts[1],
		ExactCounts[2] - Counts[2]
	};

	int32 Remaining = TotalNPCCount - Counts[0] - Counts[1] - Counts[2];
	while (Remaining > 0)
	{
		int32 BestIndex = 0;
		for (int32 Index = 1; Index < 3; ++Index)
		{
			if (FractionalRemainders[Index] > FractionalRemainders[BestIndex])
			{
				BestIndex = Index;
			}
		}

		++Counts[BestIndex];
		FractionalRemainders[BestIndex] = -1.f;
		--Remaining;
	}

	Result.FamiliarExit = Counts[0];
	Result.SocialFollowing = Counts[1];
	Result.NearestSafeExit = Counts[2];
	return Result;
}
