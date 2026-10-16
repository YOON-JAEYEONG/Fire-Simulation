// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/ComboBoxString.h"
#include "Simulation/YUFSSimulationController.h"
#include "YUFSSimHUD.generated.h"

/**
 * YUFS 시뮬레이션 HUD 위젯 베이스 클래스
 * 에디터에서 이 클래스를 부모로 하는 UMG 위젯 블루프린트를 만들어서 사용합니다.
 * 
 * 기본 제공 바인딩 함수들을 UMG에서 바로 호출할 수 있습니다.
 */
UCLASS()
class YUFS_API UYUFSSimHUD : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// ── UMG 버튼에 바인딩할 함수들 ───────────────────────────────────────
	UFUNCTION(BlueprintCallable, Category="YUFS|HUD")
	void OnStartButtonClicked();

	UFUNCTION(BlueprintCallable, Category="YUFS|HUD")
	void OnPauseButtonClicked();

	UFUNCTION(BlueprintCallable, Category="YUFS|HUD")
	void OnStopButtonClicked();

	// 시뮬레이션을 벗어나 메인 메뉴 레벨로 돌아갑니다.
	UFUNCTION(BlueprintCallable, Category="YUFS|HUD")
	void OnMainMenuButtonClicked();

	// ── 카메라 제어 버튼 바인딩 ───────────────────────────────────────
	UFUNCTION(BlueprintCallable, Category = "YUFS|HUD|Camera")
	void OnCameraOverviewButtonClicked();

	UFUNCTION(BlueprintCallable, Category = "YUFS|HUD|Camera")
	void OnCameraFireZoneButtonClicked();

	UFUNCTION(BlueprintCallable, Category = "YUFS|HUD|Camera")
	void OnCameraPlayerViewButtonClicked();

	// ── 타임라인 관찰 모드 버튼/슬라이더 바인딩 ───────────────────────
	UFUNCTION(BlueprintCallable, Category="YUFS|HUD|Timeline")
	void OnTimelinePlayButtonClicked();

	UFUNCTION(BlueprintCallable, Category="YUFS|HUD|Timeline")
	void OnTimelinePauseButtonClicked();	

	// UMG Slider의 OnValueChanged(float)에 연결합니다. Value는 0.0~1.0 기준입니다.
	UFUNCTION(BlueprintCallable, Category="YUFS|HUD|Timeline")
	void OnTimelineSliderChanged(float NormalizedValue);

	// ── UMG 텍스트/프로그레스바에 바인딩할 데이터 조회 함수들 ─────────────
	UFUNCTION(BlueprintPure, Category="YUFS|HUD")
	FText GetPhaseText() const;

	UFUNCTION(BlueprintPure, Category="YUFS|HUD")
	FText GetElapsedTimeText() const;

	UFUNCTION(BlueprintPure, Category="YUFS|HUD")
	FText GetFireCountdownText() const;

	UFUNCTION(BlueprintPure, Category="YUFS|HUD")
	FText GetNPCStatusText() const;

	UFUNCTION(BlueprintPure, Category="YUFS|HUD")
	FText GetRunProgressText() const;

	UFUNCTION(BlueprintPure, Category="YUFS|HUD")
	float GetEvacuationRatePercent() const; // 0.0 ~ 100.0 (프로그레스바용)

	UFUNCTION(BlueprintPure, Category="YUFS|HUD")
	bool IsSimulationActive() const;

	UFUNCTION(BlueprintPure, Category="YUFS|HUD")
	bool IsFireActive() const;

	UFUNCTION(BlueprintPure, Category="YUFS|HUD|Timeline")
	bool IsTimelineReviewMode() const;

	UFUNCTION(BlueprintPure, Category="YUFS|HUD|Timeline")
	bool IsTimelinePlaying() const;

	UFUNCTION(BlueprintPure, Category="YUFS|HUD|Timeline")
	float GetTimelineProgress01() const;

	UFUNCTION(BlueprintPure, Category="YUFS|HUD|Timeline")
	FText GetTimelineTimeText() const;

	// ── 화재 선택(Fire_A / Fire_B) 버튼이 참조하는 대상 액터 ───────────────
	// 위젯이 NativeConstruct 시점에 스스로 월드에서 찾아서 세팅합니다.
	// (예전에는 레벨 블루프린트의 Create Widget 노드에서 Expose on Spawn으로
	//  넘겨줬지만, SimulationController::SpawnHUD()가 C++에서 별도로 위젯을
	//  생성하면서 그 인스턴스에는 값이 전달되지 않아 None이 되는 문제가 있었습니다.)
	UPROPERTY(BlueprintReadWrite, Category="YUFS|HUD|Fire")
	AActor* FirePointA = nullptr;

	UPROPERTY(BlueprintReadWrite, Category="YUFS|HUD|Fire")
	AActor* FirePointB = nullptr;

	// ── 화재 선택 콤보박스 ───────────────────────────────────────────────
	// WBP_SimHUD의 UMG 디자이너에서 이 이름("FireSelectComboBox")과 정확히 같은
	// ComboBox(String) 위젯을 추가하면, NativeConstruct 시점에 자동으로
	// SimulationController의 FireOptions 목록을 채우고 선택 이벤트를 연결합니다.
	// 별도의 블루프린트 배선이 필요 없습니다.
	UPROPERTY(BlueprintReadOnly, Category="YUFS|HUD|Fire", meta=(BindWidgetOptional))
	TObjectPtr<class UComboBoxString> FireSelectComboBox;

	// 콤보박스가 없거나(BindWidgetOptional) 블루프린트에서 직접 콤보박스를 다루고 싶을 때
	// NativeConstruct 이후 아무 때나 다시 호출해 옵션 목록/선택 상태를 갱신할 수 있습니다.
	UFUNCTION(BlueprintCallable, Category="YUFS|HUD|Fire")
	void PopulateFireOptions();

	// 인덱스로 직접 화재를 선택합니다 (블루프린트에서 커스텀 콤보박스/버튼을 만든 경우 사용).
	UFUNCTION(BlueprintCallable, Category="YUFS|HUD|Fire")
	void OnFireOptionSelected(int32 OptionIndex);

	UFUNCTION(BlueprintPure, Category="YUFS|HUD|Fire")
	TArray<FString> GetFireOptionNames() const;

	UFUNCTION(BlueprintPure, Category="YUFS|HUD|Fire")
	int32 GetActiveFireOptionIndex() const;

	// ── Deprecated: Fire_A/Fire_B 버튼 → 콤보박스로 대체되었습니다 ─────────
	// 기존에 이미 배선된 블루프린트가 있을 수 있어 당분간 남겨둡니다.
	UFUNCTION(BlueprintCallable, Category="YUFS|HUD|Fire", meta=(DeprecatedFunction, DeprecationMessage="Use the FireSelectComboBox / OnFireOptionSelected(int32) instead."))
	void OnFireSceneAButtonClicked();

	UFUNCTION(BlueprintCallable, Category="YUFS|HUD|Fire", meta=(DeprecatedFunction, DeprecationMessage="Use the FireSelectComboBox / OnFireOptionSelected(int32) instead."))
	void OnFireSceneBButtonClicked();

	UFUNCTION(BlueprintPure, Category="YUFS|HUD|Fire", meta=(DeprecatedFunction, DeprecationMessage="Use GetActiveFireOptionIndex() instead."))
	bool IsFireSceneASelected() const;

	UFUNCTION(BlueprintPure, Category="YUFS|HUD|Fire", meta=(DeprecatedFunction, DeprecationMessage="Use GetActiveFireOptionIndex() instead."))
	bool IsFireSceneBSelected() const;

private:
	UPROPERTY()
	AYUFSSimulationController* SimController = nullptr;

	void FindSimController();
	void FindFirePoints();

	UFUNCTION()
	void OnFireOptionComboBoxChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
};
