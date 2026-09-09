#pragma once

#include "CoreMinimal.h"

/**
 * 70:20:10처럼 전체 합이 1인 집단 비율을 실제 NPC 정수 인원수로 변환한 결과.
 * 소수점 나머지가 큰 그룹부터 남는 인원을 배정해 전체 합을 항상 보존한다.
 */
struct YUFS_API FYUFSRoutePreferenceCounts
{
	int32 FamiliarExit = 0;
	int32 SocialFollowing = 0;
	int32 NearestSafeExit = 0;

	int32 Total() const
	{
		return FamiliarExit + SocialFollowing + NearestSafeExit;
	}
};

class YUFS_API FYUFSRouteAssignment
{
public:
	static FYUFSRoutePreferenceCounts CalculateCounts(
		int32 TotalNPCCount,
		float FamiliarRatio,
		float SocialRatio,
		float NearestRatio);
};
