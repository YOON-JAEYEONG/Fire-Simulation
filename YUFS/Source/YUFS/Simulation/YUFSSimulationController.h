// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Blueprint/UserWidget.h"
#include "TimerManager.h"
#include "YUFSSimulationController.generated.h"

class AYUFSLevelDataManager;
class AYUFSEvacuationNPC;
class AYUFSBinaryManager;
class AYUFSEmergencyCommSystem;
class AYUFSHeterogeneousVolume;
class ACameraActor;
class STextBlock;
class SWidget;
class USpotLightComponent;
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
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

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

	// 대기 화면에서 대표 NPC를 근접 촬영하며 모든 행동 애니메이션을
	// 자동 순환한다. 레벨 BP/HUD에서도 수동으로 켜고 끌 수 있다.
	UFUNCTION(BlueprintCallable, Category="Simulation|NPC Animation Preview")
	void StartNPCActionAnimationShowcase();

	UFUNCTION(BlueprintCallable, Category="Simulation|NPC Animation Preview")
	void StopNPCActionAnimationShowcase();

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

	// 같은 지점 또는 너무 가까운 NPC를 건물 바닥 후보에 자동 분산한다.
	// NavMesh가 있으면 XY 후보를 보정하고, 없으면 정적 바닥 충돌만으로 동작한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simulation|NPC Distribution")
	bool bDistributeOverlappingNPCs = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simulation|NPC Distribution", meta=(ClampMin="50.0"))
	float NPCDistributionClusterRadiusCm = 180.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simulation|NPC Distribution", meta=(ClampMin="80.0"))
	float NPCDistributionSpacingCm = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simulation|NPC Distribution", meta=(ClampMin="64", ClampMax="1024"))
	int32 NPCDistributionMaxPlacementAttempts = 256;

	// 감지된 실내 바닥 높이 중 몇 개 층에 NPC를 균등 분배할지 지정한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simulation|NPC Distribution", meta=(ClampMin="1", ClampMax="8"))
	int32 NPCDistributionTargetFloorCount = 2;

	// CAD 슬래브의 수 cm 높이 차이를 같은 층으로 묶는 허용치다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simulation|NPC Distribution", meta=(ClampMin="20.0"))
	float IndoorFloorGroupingToleranceCm = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simulation|NPC Distribution", meta=(ClampMin="100.0"))
	float IndoorFloorSurfaceMinExtentCm = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simulation|NPC Distribution", meta=(ClampMin="10.0"))
	float IndoorFloorSurfaceMaxHalfThicknessCm = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simulation|NPC Distribution", meta=(ClampMin="200.0"))
	float NPCDistributionMaxRadiusCm = 3000.f;

	// 외부 NavMesh를 제외하기 위해 정적 바닥과 천장이 모두 있는 지점만 사용한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simulation|NPC Distribution")
	bool bRequireIndoorNPCPlacement = true;

	// CAD 천장에 충돌이 있으면 천장이 확인된 후보를 먼저 사용한다.
	// 충돌이 없는 건물은 동일 층의 정적 바닥 후보로 자동 폴백한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simulation|NPC Distribution")
	bool bPreferCeilingCollisionForIndoorPlacement = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simulation|NPC Distribution", meta=(ClampMin="200.0"))
	float IndoorCeilingTraceHeightCm = 1500.f;

	// 천장 충돌이 없는 CAD 건물에서는 동·서·남·북 모두 벽에 막힌 지점만 실내로 인정한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simulation|NPC Distribution", meta=(ClampMin="500.0"))
	float IndoorEnclosureTraceDistanceCm = 5000.f;

	// 후보가 할당된 층이 아닌 다른 슬래브로 스냅되는 것을 막는 높이 허용치다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simulation|NPC Distribution", meta=(ClampMin="10.0"))
	float NPCDistributionMaxFloorDeltaCm = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simulation|NPC Distribution", meta=(ClampMin="0.0"))
	float NPCDistributionDelaySeconds = 0.25f;

	// 대기 화면에서 NPC들을 11개 행동에 순서대로 배정해 애니메이션과
	// 머리 위 Action/Anim 라벨을 검수할 수 있게 한다. 시뮬레이션 시작 시
	// 자동 해제되므로 AI 결정에는 영향을 주지 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simulation|NPC Animation Preview")
	bool bPreviewAllNPCActionAnimations = true;

	// 분산 배치가 끝나면 대표 NPC 앞으로 카메라를 이동하고 11개 행동을
	// 자동 순환한다. Start Simulation을 누르면 원래 카메라로 복귀한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simulation|NPC Animation Preview")
	bool bAutoFocusNPCActionAnimationShowcase = true;

	// 근접 검수에서는 대표 NPC 한 명만 보여 행동 차이를 명확히 한다.
	// 시뮬레이션 시작 시 숨겼던 NPC를 모두 즉시 복원한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simulation|NPC Animation Preview")
	bool bIsolateFocusedNPCInAnimationShowcase = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simulation|NPC Animation Preview", meta=(ClampMin="1.0", ClampMax="15.0"))
	float NPCActionPreviewSecondsPerAction = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simulation|NPC Animation Preview", meta=(ClampMin="80.0", ClampMax="600.0"))
	float NPCActionPreviewCameraDistanceCm = 240.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Simulation|NPC Animation Preview", meta=(ClampMin="30.0", ClampMax="180.0"))
	float NPCActionPreviewLookAtHeightCm = 90.f;

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
	FTimerHandle NPCDistributionTimerHandle;

	UPROPERTY(Transient)
	TObjectPtr<AYUFSEvacuationNPC> NPCActionPreviewFocusNPC = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> NPCActionPreviewCamera = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<USpotLightComponent> NPCActionPreviewLight = nullptr;

	TWeakObjectPtr<AActor> SavedNPCActionPreviewViewTarget;
	TArray<TWeakObjectPtr<AYUFSEvacuationNPC>> NPCActionPreviewTemporarilyHiddenNPCs;
	TSharedPtr<SWidget> NPCActionPreviewOverlayWidget;
	TSharedPtr<STextBlock> NPCActionPreviewOverlayText;
	FRotator SavedNPCActionPreviewRotation = FRotator::ZeroRotator;
	float NPCActionPreviewAccumulator = 0.f;
	int32 NPCActionPreviewIndex = 0;
	bool bNPCActionAnimationShowcaseActive = false;

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
	void ScheduleNPCDistribution();
	void DistributeRegisteredNPCs();
	void ApplyNPCActionAnimationPreview(const TArray<AYUFSEvacuationNPC*>& NPCs);
	void TickNPCActionAnimationShowcase(float DeltaTime);
	void ApplyCurrentNPCActionAnimationShowcaseStep();
	void UpdateNPCActionAnimationShowcaseCamera();
	void DrawNPCActionAnimationShowcaseOverlay() const;
	bool TryResolveIndoorNPCSpawnLocation(
		const FVector& DesiredLocation,
		AYUFSEvacuationNPC* NPC,
		bool bRequireCeiling,
		FVector& OutLocation) const;
};
