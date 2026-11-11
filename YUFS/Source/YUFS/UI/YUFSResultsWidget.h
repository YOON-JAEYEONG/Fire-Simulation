#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Simulation/YUFSSimulationController.h"
#include "YUFSResultsWidget.generated.h"

class UYUFSGameInstance;

/**
 * YUFS 결과 보기 위젯 베이스 클래스.
 *
 * 메인 메뉴 "결과 보기" 버튼(UYUFSMainMenuWidget::ShowResults)에서 이 클래스를 부모로 하는
 * UMG 위젯 블루프린트(WBP_Results)를 만들어 표시하세요. NativeConstruct에서 GameInstance의
 * LastRunResults를 자동으로 불러옵니다.
 *
 * [뒤로] 버튼 → OnClosed() (BP에서 메인 메뉴 패널로 복귀 구현)
 */
UCLASS()
class YUFS_API UYUFSResultsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

	// 표시 중인 회차별 결과. RefreshResults()로 GameInstance에서 다시 불러올 수 있습니다.
	UPROPERTY(BlueprintReadOnly, Category = "YUFS|Results")
	TArray<FSimRunResult> Results;

	// GameInstance::LastRunResults를 다시 읽어 Results를 갱신합니다.
	UFUNCTION(BlueprintCallable, Category = "YUFS|Results")
	void RefreshResults();

	UFUNCTION(BlueprintPure, Category = "YUFS|Results")
	bool HasResults() const { return Results.Num() > 0; }

	UFUNCTION(BlueprintPure, Category = "YUFS|Results")
	int32 GetRunCount() const { return Results.Num(); }

	// ── 회차별 조회 (Index = Results 배열 순번) ───────────────────────
	UFUNCTION(BlueprintPure, Category = "YUFS|Results")
	FSimRunResult GetRunAt(int32 Index) const;

	UFUNCTION(BlueprintPure, Category = "YUFS|Results")
	FText GetRunSummaryText(int32 Index) const;

	// ── 전체 회차 집계 ─────────────────────────────────────────────────
	UFUNCTION(BlueprintPure, Category = "YUFS|Results")
	float GetAverageEvacuationRate() const; // [0,1]

	UFUNCTION(BlueprintPure, Category = "YUFS|Results")
	float GetAverageDurationSeconds() const;

	UFUNCTION(BlueprintPure, Category = "YUFS|Results")
	float GetAverageEvacuationTime() const;

	UFUNCTION(BlueprintPure, Category = "YUFS|Results")
	int32 GetTotalEvacuatedCount() const;

	UFUNCTION(BlueprintPure, Category = "YUFS|Results")
	int32 GetTotalIncapacitatedCount() const;

	UFUNCTION(BlueprintPure, Category = "YUFS|Results")
	int32 GetTotalNPCCount() const;

	UFUNCTION(BlueprintPure, Category = "YUFS|Results")
	FSimRunResult GetBestRun() const; // 대피율 최고 회차

	UFUNCTION(BlueprintPure, Category = "YUFS|Results")
	FSimRunResult GetWorstRun() const; // 대피율 최저 회차

	UFUNCTION(BlueprintPure, Category = "YUFS|Results")
	FText GetSummaryText() const;

	// ListView 바인딩용 — 회차별 결과를 UObject 항목으로 감싸서 반환합니다.
	// WBP_Results에서: ListView -> Set List Items(Get Result Entries), EntryWidgetClass = WBP_ResultRow(YUFSResultRowWidget).
	// RefreshResults() 이후(OnResultsRefreshed에서) 호출하세요.
	UFUNCTION(BlueprintCallable, Category = "YUFS|Results")
	TArray<UObject*> GetResultEntries();

	// 배치 전체를 다시 실행하고 싶다면 UYUFSMainMenuWidget::OnContinueLastClicked()
	// ("이어서 실행")가 이미 동일한 기능을 제공합니다. 특정 회차만 재현하려면
	// WBP_ResultRow(YUFSResultRowWidget)의 ReplayThisRun()을 쓰세요.

	// ── BP에서 구현하는 훅 ────────────────────────────────────────────
	// Results 갱신 후 호출 → BP에서 리스트/그래프 위젯을 다시 그립니다.
	UFUNCTION(BlueprintImplementableEvent, Category = "YUFS|Results")
	void OnResultsRefreshed();

	// [뒤로] 버튼에서 호출 → 메인 메뉴 패널로 복귀
	UFUNCTION(BlueprintImplementableEvent, Category = "YUFS|Results")
	void OnClosed();

protected:
	UYUFSGameInstance* GetYUFSGameInstance() const;
};
