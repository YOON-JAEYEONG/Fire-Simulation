#pragma once

#include "Communication/YUFSCommTypes.h"
#include "CoreMinimal.h"
#include "Core/YUFSObservation.h"
#include "Core/YUFSTypes.h"
#include "GameFramework/Character.h"
#include "NPC/Decision/YUFSOnnxPolicy.h"
#include "YUFSEvacuationNPC.generated.h"

class UAnimMontage;
class AYUFSLevelDataManager;
class AYUFSBinaryManager;
class UYUFSSocialInfluenceComponent;
class UYUFSSmokeAwareNavigator;
class UYUFSBehaviorStateMachine;
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

	// ── 통신 상태 접근자 ──────────────────────────────────────────────
	bool    IsAlarmSounding()           const { return bAlarmSounding; }
	bool    HasReceivedStaffGuidance()  const { return bReceivedStaffGuidance; }
	bool    HasReceivedPreRecordedMsg() const { return bReceivedPreRecordedMsg; }
	bool    HasReceivedLiveAnnouncement()const{ return bReceivedLiveAnnouncement; }
	FVector GetStaffGuidedExitLocation()const { return StaffGuidedExitLocation; }
	FVector GetSpawnLocation()          const { return SpawnLocation; }

	const FYUFSNPCObservation& GetLastObservation() const { return PrevObservation; }
	EYUFSAction GetLastAction() const { return CurrentAction; }
	void NotifyEpisodeFinished(EYUFSTerminalReason TerminalReason);

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

	// ── CSV 로깅 ──────────────────────────────────────────────────────
	FYUFSNPCObservation PrevObservation{};
	bool bHasPendingTransition = false;
	int32 TransitionStepIndex = 0;

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

	// ── 액션 실행 상태 (구 BT 노드 메모리 대체) ───────────────────────
	EYUFSAction CurrentAction            = EYUFSAction::Idle;
	float PolicyTickAccumulator          = 0.f;
	float ActionHoldTimer                = 0.f;
	EYUFSBehaviorState LastPolicyBehaviorState = EYUFSBehaviorState::Normal;
	FVector CurrentNavTarget             = FVector::ZeroVector;
	float LookAnchorYaw                  = 0.f;
	float LookElapsed                    = 0.f;

	static constexpr float PolicyTickInterval    = 0.1f;
	static constexpr float MinActionHoldDuration = 2.0f;

	// ── 내부 함수 ─────────────────────────────────────────────────────
	int32 GetCurrentSimFrame() const;
	void BuildObservation(FYUFSNPCObservation& Out) const;
	void FlushLearningTransition(const FYUFSNPCObservation& NextObs, EYUFSTerminalReason TerminalReason);
	EYUFSTerminalReason GetCurrentTerminalReason() const;
	void UpdateStuckDetection(float DeltaTime);

	// ── MLP 정책 실행 ─────────────────────────────────────────────────
	void TickPolicy(float DeltaTime);
	void OnActionChanged(EYUFSAction NewAction);
	void ExecuteCurrentAction(float DeltaTime);
	FVector ResolveNavigationTarget(EYUFSAction Action) const;
	static bool IsNavigationAction(EYUFSAction Action);
};
