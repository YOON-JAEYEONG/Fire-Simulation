#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Core/YUFSTypes.h"
#include "YUFSNPCAnimInstance.generated.h"

class AYUFSEvacuationNPC;

UCLASS()
class YUFS_API UYUFSNPCAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	// ── 이동 ──────────────────────────────────────────────────────────────
	UPROPERTY(BlueprintReadOnly, Category="Movement")
	float Speed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Movement")
	bool bIsMoving = false;

	// ── 행동 상태 ─────────────────────────────────────────────────────────
	UPROPERTY(BlueprintReadOnly, Category="State")
	EYUFSBehaviorState BehaviorState = EYUFSBehaviorState::Normal;

	UPROPERTY(BlueprintReadOnly, Category="State")
	EYUFSAction LastAction = EYUFSAction::Idle;

	UPROPERTY(BlueprintReadOnly, Category="State")
	bool bIsCrawling = false;

	UPROPERTY(BlueprintReadOnly, Category="State")
	bool bIsIncapacitated = false;

	// ── 애니메이션 트리거 ─────────────────────────────────────────────────
	// ABP에서 Cough 몽타주 트리거 조건으로 사용
	UPROPERTY(BlueprintReadOnly, Category="Trigger")
	bool bIsCoughing = false;

	// SeekInformation 상태에서의 두리번 블렌딩용
	UPROPERTY(BlueprintReadOnly, Category="Trigger")
	bool bIsLookingAround = false;

	// Film 행동. 발생 비율은 시나리오별로 캘리브레이션한다.
	UPROPERTY(BlueprintReadOnly, Category="Trigger")
	bool bIsFilming = false;

	// ── 연기/스트레스 블렌딩 ─────────────────────────────────────────────
	// 누적 연기 흡입량 [0,1] — 미세 비틀거림·호흡 애니메이션 블렌딩
	UPROPERTY(BlueprintReadOnly, Category="Blend")
	float SmokeExposure = 0.f;

	// 위험 인식 수준 [0,1] — 긴장 포즈 블렌딩
	UPROPERTY(BlueprintReadOnly, Category="Blend")
	float RiskPerception = 0.f;

private:
	AYUFSEvacuationNPC* OwnerNPC = nullptr;
};
