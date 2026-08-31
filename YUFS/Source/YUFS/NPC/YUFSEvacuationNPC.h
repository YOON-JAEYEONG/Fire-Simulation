#pragma once

#include "Communication/YUFSCommTypes.h"
#include "CoreMinimal.h"
#include "Core/YUFSDeterministicRng.h"
#include "Core/YUFSObservation.h"
#include "Core/YUFSTypes.h"
#include "GameFramework/Character.h"
#include "NPC/Decision/YUFSOnnxPolicy.h"
#include "Simulation/YUFSTimelineTypes.h"
#include "YUFSEvacuationNPC.generated.h"

class UAnimMontage;
class AYUFSLevelDataManager;
class AYUFSBinaryManager;
class UYUFSSocialInfluenceComponent;
class UYUFSSmokeAwareNavigator;
class UYUFSBehaviorStateMachine;
class UYUFSBeliefComponent;
class UYUFSIntentComponent;
class UYUFSActionTaskComponent;
class UYUFSActionAnimationComponent;
class UYUFSNPCDebugComponent;
class UYUFSNPCPerceptionComponent;
class AYUFSSimulationController;

UCLASS()
class YUFS_API AYUFSEvacuationNPC : public ACharacter
{
	GENERATED_BODY()
	AYUFSEvacuationNPC();

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

	UFUNCTION()
	void OnCommReceived(EYUFSCommType CommType, FVector SourceLocation, float EffectiveRadius, FVector GuidanceTarget);

	// ── 에디터 편집 ─────────────────────────────────────────────────────
	UPROPERTY(EditAnywhere, Category="Animation")
	UAnimMontage* CoughMontage = nullptr;

	UPROPERTY(EditAnywhere, Category="Animation")
	UAnimMontage* FilmMontage = nullptr;

	UPROPERTY(EditAnywhere, Category="AI|Policy")
	bool bDataCollectionMode = false;

	UPROPERTY(EditAnywhere, Category="AI|Logging")
	bool bLogTransitions = true;

	// ── 근거 기반 결정 모델 ───────────────────────────────────────────
	UPROPERTY(EditAnywhere, Category="AI|Evidence Decision")
	bool bEnableEvidenceDecisionModel = true;

	UPROPERTY(EditAnywhere, Category="AI|Evidence Decision")
	bool bLogDecisionTrace = true;

	// 배치 NPC는 에디터에서 명시할 수 있고, 미지정 시 Actor 경로 CRC로 결정한다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="AI|Determinism")
	int32 StableNPCId = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="AI|Determinism")
	int32 ScenarioSeed = 20260831;

	// 비용이 큰 감지/근접 NPC 탐색은 렌더 프레임마다 수행하지 않는다.
	// NPC별 초기 위상을 달리해 같은 프레임에 갱신이 몰리지 않게 한다.
	UPROPERTY(EditAnywhere, Category="AI|Optimization", meta=(ClampMin="0.05"))
	float PerceptionUpdateIntervalSeconds = 0.2f;

	UPROPERTY(EditAnywhere, Category="AI|Optimization", meta=(ClampMin="0.05"))
	float SocialUpdateIntervalSeconds = 0.2f;

	// 경험 전이는 정책 주기와 같은 최대 10Hz로 기록한다.
	UPROPERTY(EditAnywhere, Category="AI|Logging", meta=(ClampMin="0.1"))
	float TransitionLogIntervalSeconds = 0.1f;

	// ── 이동 보조 API ──────────────────────────────────────────────────
	void DriveMovementToward(FVector Target);
	void SetMovementSpeed(float Speed);

	// ── 컴포넌트 접근자 ────────────────────────────────────────────────
	UYUFSBehaviorStateMachine*   GetBehaviorStateMachine() const { return BehaviorSM; }
	UYUFSSmokeAwareNavigator*    GetNavigator()             const { return Navigator; }
	UYUFSNPCPerceptionComponent* GetNPCPerceptionComponent()const { return PerceptionComp; }
	UYUFSSocialInfluenceComponent* GetSocialComponent()     const { return SocialComp; }
	AYUFSLevelDataManager*       GetLevelDataManager()      const { return LevelDataMgr; }
	AYUFSBinaryManager*          GetBinaryManager()         const { return BinaryManager; }
	UYUFSBeliefComponent*        GetBeliefComponent()       const { return BeliefComp; }
	UYUFSIntentComponent*        GetIntentComponent()       const { return IntentComp; }
	UYUFSActionTaskComponent*    GetActionTaskComponent()   const { return ActionTaskComp; }
	UYUFSActionAnimationComponent* GetActionAnimationComponent() const { return ActionAnimationComp; }
	EYUFSIntent GetCurrentIntent() const;
	int32 GetStableNPCId() const { return StableNPCId; }
	bool RollSocialProbability(float Probability);
	void ApplyDistributedSpawnLocation(const FVector& NewLocation);

	// ── 통신 상태 접근자 ──────────────────────────────────────────────
	bool    IsAlarmSounding()           const { return bAlarmSounding; }
	bool    HasReceivedStaffGuidance()  const { return bReceivedStaffGuidance; }
	bool    HasReceivedPreRecordedMsg() const { return bReceivedPreRecordedMsg; }
	bool    HasReceivedLiveAnnouncement()const{ return bReceivedLiveAnnouncement; }
	FVector GetStaffGuidedExitLocation()const { return StaffGuidedExitLocation; }
	FVector GetSpawnLocation()          const { return SpawnLocation; }

	const FYUFSNPCObservation& GetLastObservation() const { return PrevObservation; }
	EYUFSAction GetLastAction() const { return CurrentAction; }
	EYUFSAction GetDisplayedAction() const { return bActionAnimationPreviewActive ? PreviewAction : CurrentAction; }
	bool IsActionAnimationPreviewActive() const { return bActionAnimationPreviewActive; }
	FString GetCurrentActionAnimationName() const;

	UFUNCTION(BlueprintCallable, Category="NPC|Animation")
	void SetActionAnimationPreview(EYUFSAction Action);

	UFUNCTION(BlueprintCallable, Category="NPC|Animation")
	void ClearActionAnimationPreview();

	void NotifyEpisodeFinished(EYUFSTerminalReason TerminalReason);

	// ── 타임라인 기록/관찰 모드 API ───────────────────────────────────
	// 현재 NPC 상태를 최소 정보 스냅샷으로 변환합니다.
	FYUFSTimelineNPCSnapshot BuildTimelineSnapshot() const;

	// 관찰 모드에서 저장된 스냅샷을 Actor에 적용합니다.
	void ApplyTimelineSnapshot(const FYUFSTimelineNPCSnapshot& Snapshot);

	// 관찰 모드에서는 AI/이동/정책 Tick을 정지하고 기록된 Transform만 적용합니다.
	void SetTimelinePlaybackMode(bool bEnabled);
	bool IsTimelinePlaybackMode() const { return bTimelinePlaybackMode; }

private:
	UPROPERTY(VisibleAnywhere)
	UYUFSNPCPerceptionComponent* PerceptionComp;
	UPROPERTY(VisibleAnywhere)
	UYUFSBehaviorStateMachine* BehaviorSM;
	UPROPERTY(VisibleAnywhere)
	UYUFSSmokeAwareNavigator* Navigator;
	UPROPERTY(VisibleAnywhere)
	UYUFSSocialInfluenceComponent* SocialComp;
	UPROPERTY(VisibleAnywhere)
	UYUFSNPCDebugComponent* DebugComp;
	UPROPERTY(VisibleAnywhere)
	UYUFSBeliefComponent* BeliefComp;
	UPROPERTY(VisibleAnywhere)
	UYUFSIntentComponent* IntentComp;
	UPROPERTY(VisibleAnywhere)
	UYUFSActionTaskComponent* ActionTaskComp;
	UPROPERTY(VisibleAnywhere)
	UYUFSActionAnimationComponent* ActionAnimationComp;

	UPROPERTY(VisibleAnywhere)
	AYUFSBinaryManager* BinaryManager = nullptr;
	UPROPERTY(VisibleAnywhere)
	AYUFSLevelDataManager* LevelDataMgr = nullptr;
	UPROPERTY(VisibleAnywhere)
	AYUFSSimulationController* SimulationController = nullptr;

	// ── 통신 상태 ─────────────────────────────────────────────────────
	UPROPERTY(VisibleAnywhere)
	bool bAlarmSounding = false;
	UPROPERTY()
	bool bReceivedPreRecordedMsg = false;
	UPROPERTY()
	bool bReceivedLiveAnnouncement = false;
	UPROPERTY()
	bool bReceivedStaffGuidance = false;
	UPROPERTY()
	FVector StaffGuidedExitLocation = FVector::ZeroVector;

	FVector SpawnLocation = FVector::ZeroVector;

	// ── 타임라인 관찰 모드 ─────────────────────────────────────────────
	bool bTimelinePlaybackMode = false;
	float SavedWalkSpeedBeforeTimeline = 300.f;

	// ── CSV 로깅 ──────────────────────────────────────────────────────
	FYUFSNPCObservation PrevObservation{};
	bool bHasPendingTransition = false;
	int32 TransitionStepIndex = 0;
	float PerceptionUpdateAccumulator = 0.f;
	float SocialUpdateAccumulator = 0.f;
	float TransitionLogAccumulator = 0.f;

	// ── 스턱 감지 ─────────────────────────────────────────────────────
	float StuckTimer = 0.f;
	float PositionStuckTimer = 0.f;
	FVector LastMovementSampleLocation = FVector::ZeroVector;
	FVector LastPositionCheckLocation  = FVector::ZeroVector;
	bool bHasMovementSample = false;

	// ── Milling 누적 카운터 (정책 틱 단위) ────────────────────────────
	int32 MillingActionCount = 0;

	// ── MLP 정책 (ONNX 추론, RuleBasedPolicy 폴백 내장) ──────────────
	FYUFSOnnxPolicy MLPolicy;
	FYUFSDeterministicRngSet DeterministicRng;
	bool bHasSafeExit = false;
	FVector LastSafeExit = FVector::ZeroVector;

	// ── 액션 실행 상태 (구 BT 노드 메모리 대체) ───────────────────────
	EYUFSAction CurrentAction            = EYUFSAction::Idle;
	float PolicyTickAccumulator          = 0.f;
	float ActionHoldTimer                = 0.f;
	EYUFSBehaviorState LastPolicyBehaviorState = EYUFSBehaviorState::Normal;
	FVector CurrentNavTarget             = FVector::ZeroVector;
	float LookAnchorYaw                  = 0.f;
	float LookElapsed                    = 0.f;
	bool bActionAnimationPreviewActive   = false;
	EYUFSAction PreviewAction            = EYUFSAction::Idle;

	static constexpr float PolicyTickInterval    = 0.1f;
	static constexpr float MinActionHoldDuration = 2.0f;

	// ── 내부 함수 ─────────────────────────────────────────────────────
	int32 GetCurrentSimFrame() const;
	void BuildObservation(FYUFSNPCObservation& Out) const;
	void FlushLearningTransition(const FYUFSNPCObservation& NextObs, EYUFSTerminalReason TerminalReason);
	EYUFSTerminalReason GetCurrentTerminalReason() const;
	void UpdateStuckDetection(float DeltaTime);

	// ── MLP 정책 실행 ─────────────────────────────────────────────────
	void TickPolicy(float DeltaTime, const FYUFSNPCObservation& Observation);
	EYUFSAction ConstrainActionForIntent(EYUFSAction ProposedAction) const;
	void UpdateEvidenceDecisionModel(float DeltaTime, FYUFSNPCObservation& Observation);
	void TraceIntentTransition() const;
	void TraceTaskEvent(EYUFSActionTask Task, EYUFSTaskCancelReason Reason, const FString& Trigger) const;
	FString GetScenarioHash() const;
	void OnActionChanged(EYUFSAction NewAction);
	void UpdateActionAnimation(bool bForce = false);
	void ExecuteCurrentAction(float DeltaTime);
	FVector ResolveNavigationTarget(EYUFSAction Action) const;
	static bool IsNavigationAction(EYUFSAction Action);
};
