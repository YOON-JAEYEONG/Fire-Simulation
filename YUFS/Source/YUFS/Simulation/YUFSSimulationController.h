// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "YUFSSimulationController.generated.h"

class AYUFSLevelDataManager;
class AYUFSEvacuationNPC;
class AYUFSBinaryManager;
class AYUFSEmergencyCommSystem;
class AYUFSHeterogeneousVolume;
class UYUFSSimHUD;

// ── 시뮬레이션 단계 ────────────────────────────────────────────────────────
UENUM(BlueprintType)
enum class ESimPhase : uint8
{
	WaitingToStart,   // 대기 (카운트다운 중)
	FireStartDelay,   // 불이 붙기 전 (NPC들 일상 상태)
	FireActive,       // 화재 진행 중 (NPC들 대피 중)
	Completed,        // 시뮬레이션 종료 (성공/실패)
};

// ── 한 회차 결과 요약 ──────────────────────────────────────────────────────
USTRUCT(BlueprintType)
struct FSimRunResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) int32 RunIndex = 0;
	UPROPERTY(BlueprintReadOnly) int32 TotalNPCCount = 0;
	UPROPERTY(BlueprintReadOnly) int32 EvacuatedCount = 0;
	UPROPERTY(BlueprintReadOnly) int32 IncapacitatedCount = 0;
	UPROPERTY(BlueprintReadOnly) float EvacuationRate = 0.f;   // [0,1]
	UPROPERTY(BlueprintReadOnly) float SimDurationSeconds = 0.f;
	UPROPERTY(BlueprintReadOnly) float AverageEvacuationTime = 0.f;
};

// ── 이벤트 델리게이트 ──────────────────────────────────────────────────────
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSimPhaseChanged, ESimPhase, NewPhase);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSimRunCompleted, const FSimRunResult&, Result);


UCLASS()
class YUFS_API AYUFSSimulationController : public AActor
{
	GENERATED_BODY()

public:
	AYUFSSimulationController();

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

	// ── 외부 제어 API (HUD 버튼 → 이 함수 호출) ───────────────────────
	UFUNCTION(BlueprintCallable, Category="Simulation")
	void StartSimulation();

	UFUNCTION(BlueprintCallable, Category="Simulation")
	void PauseSimulation();

	UFUNCTION(BlueprintCallable, Category="Simulation")
	void ResumeSimulation();

	UFUNCTION(BlueprintCallable, Category="Simulation")
	void StopAndResetSimulation();

	// ── 상태 조회 (HUD가 읽음) ───────────────────────────────────────────
	UFUNCTION(BlueprintPure, Category="Simulation")
	ESimPhase GetCurrentPhase() const { return CurrentPhase; }

	UFUNCTION(BlueprintPure, Category="Simulation")
	float GetElapsedTime() const { return ElapsedSimTime; }

	UFUNCTION(BlueprintPure, Category="Simulation")
	float GetFireStartCountdown() const;

	UFUNCTION(BlueprintPure, Category="Simulation")
	int32 GetCurrentRunIndex() const { return CurrentRunIndex; }

	UFUNCTION(BlueprintPure, Category="Simulation")
	int32 GetEvacuatedCount() const { return LiveEvacuatedCount; }

	UFUNCTION(BlueprintPure, Category="Simulation")
	int32 GetIncapacitatedCount() const { return LiveIncapacitatedCount; }

	UFUNCTION(BlueprintPure, Category="Simulation")
	int32 GetTotalNPCCount() const { return RegisteredNPCs.Num(); }

	UFUNCTION(BlueprintPure, Category="Simulation")
	TArray<FSimRunResult> GetAllRunResults() const { return AllRunResults; }

	// ── NPC 등록 (NPC의 BeginPlay에서 자동 호출) ─────────────────────
	void RegisterNPC(AYUFSEvacuationNPC* NPC);

	// ── 이벤트 ────────────────────────────────────────────────────────
	UPROPERTY(BlueprintAssignable)
	FOnSimPhaseChanged OnPhaseChanged;

	UPROPERTY(BlueprintAssignable)
	FOnSimRunCompleted OnRunCompleted;

	// ── 편집 가능 파라미터 ───────────────────────────────────────────
	// 화재 시뮬레이션 시작까지 대기 시간 (초)
	UPROPERTY(EditAnywhere, Category="Simulation|Timing")
	float FireStartDelaySeconds = 30.f;

	// 최대 시뮬레이션 시간 (초, 이 시간이 지나면 강제 종료)
	UPROPERTY(EditAnywhere, Category="Simulation|Timing")
	float MaxSimDurationSeconds = 300.f;

	// 반복 실험 횟수
	UPROPERTY(EditAnywhere, Category="Simulation|Batch")
	int32 TotalRunCount = 1;

	// 각 회차 종료 후 다음 회차까지 대기 (초)
	UPROPERTY(EditAnywhere, Category="Simulation|Batch")
	float DelayBetweenRunsSeconds = 3.f;

	// 알람은 화재 시작 몇 초 후에 발령?
	UPROPERTY(EditAnywhere, Category="Simulation|Events")
	float AlarmTriggerOffsetSeconds = 5.f;

	// NPC가 이 거리 안에 들어오면 대피 성공으로 판정 (cm)
	UPROPERTY(EditAnywhere, Category="Simulation|Events")
	float EvacuationSuccessDistanceCm = 150.f;

	// HUD 위젯 클래스 (에디터에서 BP 위젯 클래스를 연결)
	UPROPERTY(EditAnywhere, Category="Simulation|UI")
	TSubclassOf<UUserWidget> HUDWidgetClass;

private:
	// ── 내부 상태 ─────────────────────────────────────────────────────
	ESimPhase CurrentPhase = ESimPhase::WaitingToStart;
	bool bIsPaused = false;

	float ElapsedSimTime = 0.f;    // 현재 회차 경과 시간
	float FirePhaseTimer = 0.f;    // FireStartDelay 단계 타이머
	float BetweenRunTimer = 0.f;   // 회차 사이 대기 타이머
	bool bAlarmFired = false;

	int32 CurrentRunIndex = 0;
	int32 LiveEvacuatedCount = 0;
	int32 LiveIncapacitatedCount = 0;

	TArray<AYUFSEvacuationNPC*> RegisteredNPCs;
	TArray<FSimRunResult> AllRunResults;

	// 캐싱
	AYUFSBinaryManager* BinaryManager = nullptr;
	AYUFSEmergencyCommSystem* CommSystem = nullptr;
	AYUFSHeterogeneousVolume* HeterogeneousVolume = nullptr;
	AYUFSLevelDataManager* CachedLDM = nullptr;
	UUserWidget* HUDWidgetInstance = nullptr;

	// ── 내부 함수 ─────────────────────────────────────────────────────
	void SetPhase(ESimPhase NewPhase);
	void TickFireActivePhase(float DeltaTime);
	void CheckCompletionCondition();
	void FinalizeRun();
	void StartNextRun();
	void UpdateLiveCounts();
	void SpawnHUD();
};
