// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
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

	// ── 화재 시나리오 전환 버튼 (Fire_A / Fire_B) ─────────────────────────
	// 단순 Visibility 토글이 아니라, SimulationController에게 "어느 시나리오
	// (BinaryManager+HeterogeneousVolume 데이터 쌍)를 재생할지"를 실제로 전환시킵니다.
	// Button_A / Button_B의 OnClicked를 이 함수들로 연결하세요.
	UFUNCTION(BlueprintCallable, Category="YUFS|HUD|Fire")
	void OnFireSceneAButtonClicked();

	UFUNCTION(BlueprintCallable, Category="YUFS|HUD|Fire")
	void OnFireSceneBButtonClicked();

	// UI에서 현재 선택된 시나리오를 표시(버튼 강조 등)할 때 바인딩용
	UFUNCTION(BlueprintPure, Category="YUFS|HUD|Fire")
	bool IsFireSceneASelected() const;

	UFUNCTION(BlueprintPure, Category="YUFS|HUD|Fire")
	bool IsFireSceneBSelected() const;

private:
	UPROPERTY()
	AYUFSSimulationController* SimController = nullptr;

	void FindSimController();
	void FindFirePoints();
};
