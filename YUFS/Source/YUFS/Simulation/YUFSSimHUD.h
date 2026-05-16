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

private:
	UPROPERTY()
	AYUFSSimulationController* SimController = nullptr;

	void FindSimController();
};
