#pragma once

#include "Communication/YUFSCommTypes.h"
#include "CoreMinimal.h"
#include "Core/YUFSObservation.h"
#include "Core/YUFSTypes.h"
#include "GameFramework/Character.h"
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
class AYUFSBottleneckQueueManager;

UCLASS()
class YUFS_API AYUFSEvacuationNPC : public ACharacter
{
	GENERATED_BODY()

public:
	AYUFSEvacuationNPC();

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	UFUNCTION()
	void OnCommReceived(EYUFSCommType CommType, FVector SourceLocation, float EffectiveRadius, FVector GuidanceTarget);

	// ── 에디터 편집 ─────────────────────────────────────────────────────
	UPROPERTY(EditAnywhere, Category="Animation")
	UAnimMontage* CoughMontage = nullptr;

	UPROPERTY(EditAnywhere, Category="Animation")
	UAnimMontage* FilmMontage = nullptr;

	UPROPERTY(EditAnywhere, Category="AI|Logging")
	bool bLogTransitions = true;

	// ── BT Task에서 호출하는 공개 API ──────────────────────────────────
	// 네비게이션 + 이동 입력을 묶어서 처리 (BT Task → 매 틱 호출)
	void DriveMovementToward(FVector Target);
	void SetMovementSpeed(float Speed);
	int32 GetCurrentSimFramePublic() const { return GetCurrentSimFrame(); }

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
	EYUFSAction GetLastAction() const { return LastBTAction; }
	void SetLastBTAction(EYUFSAction Action) { LastBTAction = Action; }
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
	UPROPERTY(VisibleAnywhere)
	AYUFSBottleneckQueueManager* BottleneckQueueManager = nullptr;

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
	EYUFSAction LastBTAction = EYUFSAction::Idle;

	// ── 스턱 감지 ─────────────────────────────────────────────────────
	float StuckTimer = 0.f;
	float PositionStuckTimer = 0.f;
	FVector LastMovementSampleLocation = FVector::ZeroVector;
	FVector LastPositionCheckLocation  = FVector::ZeroVector;
	bool bHasMovementSample = false;

	// ── 내부 함수 ─────────────────────────────────────────────────────
	int32 GetCurrentSimFrame() const;
	void BuildObservation(FYUFSNPCObservation& Out) const;
	void FlushLearningTransition(const FYUFSNPCObservation& NextObs, EYUFSTerminalReason TerminalReason);
	EYUFSTerminalReason GetCurrentTerminalReason() const;
	void UpdateStuckDetection(float DeltaTime);
	float CalculateTransitionReward(
		const FYUFSNPCObservation& PrevObs,
		EYUFSAction Action,
		const FYUFSNPCObservation& NextObs,
		EYUFSTerminalReason TerminalReason) const;
};
