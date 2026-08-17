// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Blueprint/UserWidget.h"
#include "YUFSSimulationController.generated.h"

class AYUFSLevelDataManager;
class AYUFSEvacuationNPC;
class AYUFSBinaryManager;
class AYUFSEmergencyCommSystem;
class AYUFSHeterogeneousVolume;
class UYUFSSimHUD;
class UYUFSTimelineRecorder;

// ── 시뮬레이션 단계 ────────────────────────────────────────────────────────
UENUM(BlueprintType)
enum class ESimPhase : uint8
{
	WaitingToStart,   // 대기 (카운트다운 중)
	FireStartDelay,   // 불이 붙기 전 (NPC들 일상 상태)
	FireActive,       // 화재 진행 중 (NPC들 대피 중)
	TimelineReview,   // 기록 종료 후 시간대 이동/관찰 모드
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
	bool IsNPCSimulationEnabled() const
	{
		// 관찰 모드에서는 NPC AI가 다시 계산되면 안 되므로 FireActive에서만 true입니다.
		return !bIsPaused && CurrentPhase == ESimPhase::FireActive;
	}

	UFUNCTION(BlueprintPure, Category="Simulation")
	int32 GetEvacuatedCount() const { return LiveEvacuatedCount; }

	UFUNCTION(BlueprintPure, Category="Simulation")
	int32 GetIncapacitatedCount() const { return LiveIncapacitatedCount; }

	UFUNCTION(BlueprintPure, Category="Simulation")
	int32 GetTotalNPCCount() const { return InitialNPCCount > 0 ? InitialNPCCount : RegisteredNPCs.Num(); }

	UFUNCTION(BlueprintPure, Category="Simulation")
	TArray<FSimRunResult> GetAllRunResults() const { return AllRunResults; }

	// ── 타임라인 기록/관찰 API ───────────────────────────────────────
	UFUNCTION(BlueprintCallable, Category="Simulation|Timeline")
	void StartTimelineRecordingSimulation(float InRecordEndFireSeconds);

	UFUNCTION(BlueprintCallable, Category="Simulation|Timeline")
	void EnterTimelineReviewMode();

	UFUNCTION(BlueprintCallable, Category="Simulation|Timeline")
	void SeekTimelineBySeconds(float FireElapsedSeconds);

	UFUNCTION(BlueprintCallable, Category="Simulation|Timeline")
	void SeekTimelineByNormalizedValue(float NormalizedValue);

	UFUNCTION(BlueprintCallable, Category="Simulation|Timeline")
	void PlayTimeline();

	UFUNCTION(BlueprintCallable, Category="Simulation|Timeline")
	void PauseTimeline();

	UFUNCTION(BlueprintPure, Category="Simulation|Timeline")
	float GetTimelineCurrentTime() const;

	UFUNCTION(BlueprintPure, Category="Simulation|Timeline")
	float GetTimelineMaxTime() const;

	UFUNCTION(BlueprintPure, Category="Simulation|Timeline")
	float GetTimelineProgress01() const;

	UFUNCTION(BlueprintPure, Category="Simulation|Timeline")
	bool IsTimelineReviewMode() const { return CurrentPhase == ESimPhase::TimelineReview; }

	UFUNCTION(BlueprintPure, Category="Simulation|Timeline")
	bool IsTimelinePlaying() const;

	// ── NPC 등록 (NPC의 BeginPlay 또는 런타임 스폰 후 호출) ──────────
	UFUNCTION(BlueprintCallable, Category="Simulation")
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

	// 사전 녹음 방송 발령 시각 (화재 시작 기준 오프셋, 음수 = 비활성)
	UPROPERTY(EditAnywhere, Category="Simulation|Events")
	float PreRecordedMsgOffsetSeconds = -1.f;

	// 실시간 안내 방송 발령 시각 (화재 시작 기준 오프셋, 음수 = 비활성)
	UPROPERTY(EditAnywhere, Category="Simulation|Events")
	float LiveAnnouncementOffsetSeconds = -1.f;

	// 스태프 직접 안내 발령 시각 (화재 시작 기준 오프셋, 음수 = 비활성)
	// 안내 목적지는 발령 시점의 가장 안전한 출구로 자동 결정
	UPROPERTY(EditAnywhere, Category="Simulation|Events")
	float StaffGuidanceOffsetSeconds = -1.f;

	// NPC가 이 거리 안에 들어오면 대피 성공으로 판정 (cm)
	UPROPERTY(EditAnywhere, Category="Simulation|Events")
	float EvacuationSuccessDistanceCm = 150.f;

	// HUD 위젯 클래스 (에디터에서 BP 위젯 클래스를 연결)
	UPROPERTY(EditAnywhere, Category="Simulation|UI")
	TSubclassOf<UUserWidget> HUDWidgetClass;

	// ── 타임라인 기록 설정 ─────────────────────────────────────────────
	// true면 StartSimulation()으로도 지정 시간까지 기록 후 관찰 모드로 전환합니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simulation|Timeline")
	bool bEnableTimelineRecording = true;

	// 화재 시작 후 몇 초까지 기록할지 설정합니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simulation|Timeline", meta=(ClampMin="0.0"))
	float TimelineRecordEndFireSeconds = 60.f;

	// 너무 많은 정보를 저장하지 않기 위한 기록 간격입니다. 0.25초면 초당 4개 스냅샷입니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simulation|Timeline", meta=(ClampMin="0.05"))
	float TimelineRecordIntervalSeconds = 0.25f;

private:
	// ── 내부 상태 ─────────────────────────────────────────────────────
	ESimPhase CurrentPhase = ESimPhase::WaitingToStart;
	bool bIsPaused = false;

	float ElapsedSimTime = 0.f;    // 현재 회차 경과 시간
	float FirePhaseTimer = 0.f;    // FireStartDelay 단계 타이머
	float BetweenRunTimer = 0.f;   // 회차 사이 대기 타이머
	bool bAlarmFired = false;
	bool bPreRecordedMsgFired = false;
	bool bLiveAnnouncementFired = false;
	bool bStaffGuidanceFired = false;

	int32 CurrentRunIndex = 0;
	int32 InitialNPCCount = 0;
	int32 LiveEvacuatedCount = 0;
	int32 LiveIncapacitatedCount = 0;
	float TotalEvacuationTime = 0.f;

	TArray<AYUFSEvacuationNPC*> RegisteredNPCs;
	TArray<FSimRunResult> AllRunResults;

	// 대피/행동불능 처리를 이미 끝낸 NPC를 기억해서 카운트 중복 증가를 막습니다.
	TSet<AYUFSEvacuationNPC*> ResolvedNPCs;

	// 캐싱
	AYUFSBinaryManager* BinaryManager = nullptr;
	AYUFSEmergencyCommSystem* CommSystem = nullptr;
	AYUFSHeterogeneousVolume* HeterogeneousVolume = nullptr;
	AYUFSLevelDataManager* CachedLDM = nullptr;
	UUserWidget* HUDWidgetInstance = nullptr;

	UPROPERTY(VisibleAnywhere, Category="Simulation|Timeline")
	UYUFSTimelineRecorder* TimelineRecorder = nullptr;

	// ── 내부 함수 ─────────────────────────────────────────────────────
	void SetPhase(ESimPhase NewPhase);
	void TickFireActivePhase(float DeltaTime);
	void CheckCompletionCondition();
	void FinalizeRun();
	void StartNextRun();
	void UpdateLiveCounts();
	void SpawnHUD();
};
