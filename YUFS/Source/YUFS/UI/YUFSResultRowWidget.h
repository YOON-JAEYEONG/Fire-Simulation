#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/IUserObjectListEntry.h"
#include "Simulation/YUFSSimulationController.h"
#include "YUFSResultRowWidget.generated.h"

/**
 * 결과 화면의 ListView 항목(행) 위젯 베이스 클래스.
 *
 * 에디터에서 이 클래스를 부모로 하는 UMG 위젯 블루프린트(WBP_ResultRow)를 만들고,
 * WBP_Results의 ListView "Entry Widget Class"로 지정하세요. ListView가
 * UYUFSResultsWidget::GetResultEntries()로 만든 UYUFSResultEntryObject 항목을 스크롤하며
 * 이 클래스를 생성/재사용하고, 그때마다 NativeOnListItemObjectSet()이 자동 호출됩니다.
 *
 * BP에서는 아래 Get*Text() 함수들을 각 TextBlock에 바인딩하기만 하면 됩니다.
 */
UCLASS()
class YUFS_API UYUFSResultRowWidget : public UUserWidget, public IUserObjectListEntry
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "YUFS|Results")
	FSimRunResult GetRunResult() const { return CachedResult; }

	// 이 회차가 실행된 시각 ("2026-09-09 14:30"). 배치 반복이 없어진 뒤로는 회차 번호 대신
	// 실행 시각으로 각 행을 구분합니다.
	UFUNCTION(BlueprintPure, Category = "YUFS|Results")
	FText GetRunIndexText() const;

	UFUNCTION(BlueprintPure, Category = "YUFS|Results")
	FText GetEvacuationSummaryText() const; // "18 / 20명 대피"

	UFUNCTION(BlueprintPure, Category = "YUFS|Results")
	FText GetEvacuationRateText() const; // "90.0%"

	UFUNCTION(BlueprintPure, Category = "YUFS|Results")
	FText GetIncapacitatedText() const; // "사망 2명"

	UFUNCTION(BlueprintPure, Category = "YUFS|Results")
	FText GetDurationText() const; // "45.3초"

	UFUNCTION(BlueprintPure, Category = "YUFS|Results")
	FText GetAverageEvacuationTimeText() const; // "평균 대피 시간 32.1초"

	// 항목 데이터가 (재)할당된 직후 호출 → BP에서 색상/막대 그래프 등 추가 표현을 갱신합니다.
	UFUNCTION(BlueprintImplementableEvent, Category = "YUFS|Results")
	void OnRowDataSet();

	// ── 이 회차만 재현 ─────────────────────────────────────────────────
	// 이 행에 저장된 시나리오 설정 스냅샷 + 랜덤 시드로, 배치 반복 없이 이 회차 하나만
	// 그대로 재현합니다.
	UFUNCTION(BlueprintCallable, Category = "YUFS|Results")
	bool ReplayThisRun();

	// [이 회차 재현] 버튼 활성/비활성 제어용
	UFUNCTION(BlueprintPure, Category = "YUFS|Results")
	bool CanReplayThisRun() const;

protected:
	virtual void NativeOnListItemObjectSet(UObject* ListItemObject) override;

	UPROPERTY(BlueprintReadOnly, Category = "YUFS|Results")
	FSimRunResult CachedResult;
};
