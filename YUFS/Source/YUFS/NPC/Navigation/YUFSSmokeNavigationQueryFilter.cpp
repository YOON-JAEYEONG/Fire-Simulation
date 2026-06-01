// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/Navigation/YUFSSmokeNavigationQueryFilter.h"

#include "NavigationData.h"
#include "NavAreas/NavArea_Obstacle.h"

// 기본값 1.0 — 연기 미감지 시 NavArea_Obstacle 비용을 기본값으로 유지
float UYUFSSmokeNavigationQueryFilter::SmokePenaltyCost = 1.f;

void UYUFSSmokeNavigationQueryFilter::UpdateSmokeCosts(AYUFSBinaryManager* /*BinaryManager*/, int32 /*CurrentFrame*/)
{
	// 연기가 경로에서 감지된 직후 호출됨 — 장애물 구역 비용을 500배로 상향해
	// 다음 FindPathSync()에서 NavArea_Obstacle 구역을 크게 우회하도록 유도.
	// BinaryManager/CurrentFrame은 향후 지점별 밀도 기반 가변 비용 구현 시 활용.
	SmokePenaltyCost = 500.f;
}

void UYUFSSmokeNavigationQueryFilter::ResetSmokeCosts()
{
	SmokePenaltyCost = 1.f;
}

void UYUFSSmokeNavigationQueryFilter::InitializeFilter(
	const ANavigationData& NavData,
	const UObject* Querier,
	FNavigationQueryFilter& Filter) const
{
	Super::InitializeFilter(NavData, Querier, Filter);

	// ANavigationData::GetNavAreaID()는 ARecastNavMesh에서 오버라이드됨.
	// NavSys를 거치지 않고 직접 NavData에서 AreaID를 조회하는 것이 올바른 UE5 API.
	const int32 ObstacleAreaID = NavData.GetAreaID(UNavArea_Obstacle::StaticClass());
	if (ObstacleAreaID != INDEX_NONE)
	{
		Filter.SetAreaCost(static_cast<uint8>(ObstacleAreaID), SmokePenaltyCost);
	}
}