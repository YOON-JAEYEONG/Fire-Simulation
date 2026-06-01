// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "YUFSSmokeNavigationQueryFilter.generated.h"

class AYUFSBinaryManager;

UCLASS()
class YUFS_API UYUFSSmokeNavigationQueryFilter : public UNavigationQueryFilter
{
	GENERATED_BODY()

public:
	// 연기 감지 시 호출 — NavArea_Obstacle 비용을 높여 다음 경로탐색이 해당 구역을 우회하도록 유도.
	// 레벨에서 연기 위험 구역에 NavModifierVolume + NavArea_Obstacle을 배치하면 실제 우회 경로가 생성됨.
	static void UpdateSmokeCosts(AYUFSBinaryManager* BinaryManager, int32 CurrentFrame);

	// 연기 비용 초기화 — 경로 탐색 완료 후 호출해 다음 정상 탐색에 영향 없도록 초기화
	static void ResetSmokeCosts();

protected:
	// 경로탐색 쿼리 직전 호출 — SmokePenaltyCost를 NavArea_Obstacle 비용으로 주입
	virtual void InitializeFilter(const ANavigationData& NavData, const UObject* Querier, FNavigationQueryFilter& Filter) const override;

private:
	// static: NavData가 캐싱한 인스턴스와 Navigator가 가진 인스턴스가 동일한 값을 공유
	static float SmokePenaltyCost;
};