#pragma once

#include "CoreMinimal.h"
#include "Core/YUFSTypes.h"
#include "Fire/YUFSFireExtinguisher.h"
#include "YUFSTimelineTypes.generated.h"

/**
 * 타임라인 관찰 모드에서 NPC 한 명을 복원하기 위한 최소 정보입니다.
 * 화재 Grid 전체, NavMesh 경로, 센서 상세값은 저장하지 않습니다.
 */
USTRUCT(BlueprintType)
struct FYUFSTimelineNPCSnapshot
{
	GENERATED_BODY()

	// Original actor name for diagnostics/legacy fallback. StableNPCId handles map reloads.
	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	FName NPCId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	int32 StableNPCId = INDEX_NONE;

	// 관찰 모드에서 NPC 위치/방향을 복원하기 위한 최소 Transform 정보입니다.
	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	FRotator Rotation = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	FVector Velocity = FVector::ZeroVector;

	// Capture the visible action independently of the policy (walking can be Idle).
	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	EYUFSAction DisplayAction = EYUFSAction::Idle;

	UPROPERTY()
	FSoftObjectPath AnimationAssetPath;
	UPROPERTY()
	float AnimationPositionSeconds = 0.f;
	UPROPERTY()
	float AnimationPlayRate = 1.f;
	UPROPERTY()
	float AnimationLengthSeconds = 0.f;
	UPROPERTY()
	bool bAnimationLooping = true;
	UPROPERTY()
	bool bHasAnimationState = false;

	// 상태/행동 표시용 정보입니다. 관찰 모드에서는 AI를 다시 계산하지 않습니다.
	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	EYUFSBehaviorState BehaviorState = EYUFSBehaviorState::Normal;

	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	EYUFSAction CurrentAction = EYUFSAction::Idle;

	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	EYUFSIntent Intent = EYUFSIntent::Observe;

	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	EYUFSActionTask ActionTask = EYUFSActionTask::None;

	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	float CommitProbability = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	int32 PreActionCompletedCount = 0;

	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	int32 PreActionTargetCount = 0;

	// HUD/디버그 표시용 최소 위험 정보입니다.
	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	float RiskPerception = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	float SmokeExposure = 0.f;

	// 대피/행동불능 NPC는 Destroy하지 않고 숨긴 뒤, 이 값으로 복원합니다.
	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	bool bVisible = true;

	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	bool bEvacuated = false;

	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	bool bIncapacitated = false;

	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	bool bLowPosture = false;
	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	float CapsuleHalfHeightCm = 0.f;
	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	float LowBreathingHeightCm = 55.f;
};

USTRUCT(BlueprintType)
struct FYUFSTimelineDoorSnapshot
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline") FName DoorId = NAME_None;
	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline") float OpenFraction = 0.f;
	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline") float SwingDirection = 1.f;
	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline") bool bVisible = true;
};

USTRUCT(BlueprintType)
struct FYUFSTimelineExtinguisherSnapshot
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline") FName ToolId = NAME_None;
	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline") FTransform Transform = FTransform::Identity;
	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline") EYUFSFireExtinguisherState State = EYUFSFireExtinguisherState::Available;
	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline") float RemainingAgent = 0.f;
	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline") bool bVisible = true;
	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline") bool bSprayVisible = false;
	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline") FTransform SprayRelativeTransform = FTransform::Identity;
};

/**
 * 특정 시각의 전체 시뮬레이션 스냅샷입니다.
 * 화재 상황은 FireFrame만 저장하고, 실제 화재 데이터는 기존 smoke_data.bin을 다시 사용합니다.
 */
USTRUCT(BlueprintType)
struct FYUFSTimelineFrame
{
	GENERATED_BODY()

	// 화재 시작 기준 시간입니다. 음수는 화재 전 카운트다운이며 슬라이더에 포함됩니다.
	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	float FireElapsedTime = 0.f;

	// HeterogeneousVolume/BinaryManager가 참조할 화재 프레임 번호입니다.
	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	int32 FireFrame = 0;

	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	int32 EvacuatedCount = 0;

	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	int32 IncapacitatedCount = 0;
	// Preserve the population and simulation clock at capture time. A later
	// placement/withdrawal or timing setting must not rewrite an earlier HUD.
	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	int32 TotalNPCCount = 0;
	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	float SimulationElapsedTime = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	TArray<FYUFSTimelineNPCSnapshot> NPCSnapshots;
	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline") TArray<FYUFSTimelineDoorSnapshot> DoorSnapshots;
	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline") TArray<FYUFSTimelineExtinguisherSnapshot> ExtinguisherSnapshots;
};
