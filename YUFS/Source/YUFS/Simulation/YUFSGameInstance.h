// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "YUFSScenarioConfig.h"
#include "YUFSSimulationController.h"
#include "YUFSRunResultsSaveGame.h"
#include "YUFSGameInstance.generated.h"

// 레벨 리로드를 넘어 시나리오 설정 / 회차 결과를 보존하는 GameInstance
UCLASS()
class YUFS_API UYUFSGameInstance : public UGameInstance
{
	GENERATED_BODY()

protected:
	virtual void Init() override;

public:
	// ── 결과 영속화 (디스크 저장, 프로그램 재시작 후에도 전체 기록 확인 가능) ──
	// 현재 LastRunResults(누적 기록 전체)를 디스크에 저장합니다.
	UFUNCTION(BlueprintCallable, Category = "YUFS|Scenario")
	void SaveRunResultsToDisk() const;

	// ── 시나리오 설정 (메인 메뉴 → 시뮬레이션 레벨) ─────────────────────
	// 한 번이라도 메뉴에서 시나리오를 실행했으면 true. 레벨이 리로드되어도
	// SimulationController가 ActiveScenario를 다시 적용할 수 있게 유지됩니다.
	// 메뉴 없이 바로 PIE로 레벨에 들어온 경우 false → 컨트롤러의 기본값 사용.
	UPROPERTY(BlueprintReadOnly, Category = "YUFS|Scenario")
	bool bHasActiveScenario = false;

	UPROPERTY(BlueprintReadOnly, Category = "YUFS|Scenario")
	FYUFSScenarioConfig ActiveScenario;

	// 지금까지 실행한 모든 회차의 누적 기록 (결과 화면에서 사용). 회차마다 자기 Timestamp를
	// 가지고 있어서 여러 세션에 걸친 기록이라도 구분해서 보여줄 수 있습니다.
	UPROPERTY(BlueprintReadOnly, Category = "YUFS|Scenario")
	TArray<FSimRunResult> LastRunResults;

	// 시나리오를 확정하고 해당 맵을 로드합니다. (메인 메뉴 / 시나리오 설정 화면에서 호출)
	UFUNCTION(BlueprintCallable, Category = "YUFS|Scenario")
	void LaunchScenario(const FYUFSScenarioConfig& Config);

	// 이전에 실행한 시나리오를 그대로 다시 실행합니다.
	UFUNCTION(BlueprintCallable, Category = "YUFS|Scenario")
	bool RelaunchLastScenario();

	// SimulationController가 회차 종료 시 결과를 넘겨줍니다.
	UFUNCTION(BlueprintCallable, Category = "YUFS|Scenario")
	void StoreRunResults(const TArray<FSimRunResult>& Results);

	// ── 특정 회차 재현 ───────────────────────────────────────────────────
	// SimulationController::StartSimulation()이 이번 실행 시작 시 소비합니다.
	// 소비 후에는 자동으로 false로 돌아갑니다.
	UPROPERTY(BlueprintReadOnly, Category = "YUFS|Scenario")
	bool bHasPendingReplaySeed = false;

	UPROPERTY(BlueprintReadOnly, Category = "YUFS|Scenario")
	int32 PendingReplaySeed = 0;

	// 재현할 회차의 NPC별 초기 Transform/클래스. bHasPendingReplaySeed와 함께 소비됩니다.
	UPROPERTY(BlueprintReadOnly, Category = "YUFS|Scenario")
	TArray<FTransform> PendingReplayNPCTransforms;

	UPROPERTY(BlueprintReadOnly, Category = "YUFS|Scenario")
	TArray<TSubclassOf<AYUFSEvacuationNPC>> PendingReplayNPCClasses;

	// 결과 화면에서 특정 회차(RunResult)를 골라 "이 회차만 재현"할 때 호출합니다.
	// 같은 시나리오 설정 + 그 회차의 랜덤 시드로 단일 회차(1회)만 다시 실행합니다.
	UFUNCTION(BlueprintCallable, Category = "YUFS|Scenario")
	bool ReplayRun(const FSimRunResult& RunResult);

	// 시뮬레이션 레벨(SimHUD 등)에서 호출 → 메인 메뉴 레벨로 복귀합니다.
	// ActiveScenario/LastRunResults는 유지하여 "이어서 실행"/결과 화면에서 계속 쓸 수 있게 합니다.
	UFUNCTION(BlueprintCallable, Category = "YUFS|Scenario")
	void ReturnToMainMenu();

	// ── 설정 화면 ────────────────────────────────────────────────────────
	// NPC 디버그 오버레이(상태/위험도/경로 표시) 전역 표시 여부. 각 NPC의
	// UYUFSNPCDebugComponent::bEnabled와 AND로 결합됩니다(둘 다 켜져 있어야 표시).
	// 기본값 true — 설정 화면에서 끄기 전까지는 기존 동작(항상 표시)과 동일합니다.
	UPROPERTY(BlueprintReadOnly, Category = "YUFS|Settings")
	bool bNPCDebugOverlayEnabled = true;

private:
	void LoadRunResultsFromDisk();
};
