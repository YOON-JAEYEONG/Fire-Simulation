// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Communication/YUFSCommTypes.h"
#include "CoreMinimal.h"
#include "Core/YUFSObservation.h"
#include "Decision/YUFSPolicyFactory.h"
#include "GameFramework/Character.h"
#include "YUFSEvacuationNPC.generated.h"

class AYUFSLevelDataManager;
class AYUFSBinaryManager;
class UYUFSSocialInfluenceComponent;
class UYUFSSmokeAwareNavigator;
class UYUFSBehaviorStateMachine;
class UYUFSNPCDebugComponent;
class UYUFSNPCPerceptionComponent;

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

	UPROPERTY(EditAnywhere, Category="AI")
	EPolicyType PolicyType = EPolicyType::RuleBased;

	UYUFSBehaviorStateMachine* GetBehaviorStateMachine() const { return BehaviorSM; }
	UYUFSSmokeAwareNavigator* GetNavigator() const { return Navigator; }
	const FYUFSNPCObservation& GetLastObservation() const { return PrevObservation; }
	EYUFSAction GetLastAction() const { return LastAction; }

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
	AYUFSBinaryManager* BinaryManager;
	UPROPERTY(VisibleAnywhere)
	AYUFSLevelDataManager* LevelDataMgr;

	TSharedPtr<IYUFSDecisionPolicy> DecisionPolicy;

	FYUFSNPCObservation PrevObservation{};
	EYUFSAction LastAction = EYUFSAction::Idle;

	int32 GetCurrentSimFrame() const;
	void BuildObservation(FYUFSNPCObservation& Out) const;
	void ExecuteAction(EYUFSAction Action);
	float CalculateCurrentReward(const FYUFSNPCObservation& Obs, EYUFSAction Action) const;
	void UpdateLookingAround(float DeltaTime);

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

	float StuckTimer = 0.0f;
	float LookAroundAnchorYaw = 0.f;
	float LookAroundElapsedTime = 0.f;
	bool bWasLookingAroundLastFrame = false;
};
