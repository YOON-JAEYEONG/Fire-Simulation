#pragma once

#include "CoreMinimal.h"
#include "Core/YUFSTypes.h"
#include "YUFSTimelineTypes.generated.h"

/**
 * 타임라인 관찰 모드에서 NPC 한 명을 복원하기 위한 최소 정보입니다.
 * 화재 Grid 전체, NavMesh 경로, 센서 상세값은 저장하지 않습니다.
 */
USTRUCT(BlueprintType)
struct FYUFSTimelineNPCSnapshot
{
	GENERATED_BODY()

	// NPC를 다시 찾기 위한 ID입니다. 현재는 Actor의 FName을 사용합니다.
	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	FName NPCId = NAME_None;

	// 관찰 모드에서 NPC 위치/방향을 복원하기 위한 최소 Transform 정보입니다.
	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	FRotator Rotation = FRotator::ZeroRotator;

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
};

/**
 * 특정 시각의 전체 시뮬레이션 스냅샷입니다.
 * 화재 상황은 FireFrame만 저장하고, 실제 화재 데이터는 기존 smoke_data.bin을 다시 사용합니다.
 */
USTRUCT(BlueprintType)
struct FYUFSTimelineFrame
{
	GENERATED_BODY()

	// 화재 시작 후 경과 시간입니다. 슬라이더 기준 시간으로 사용합니다.
	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	float FireElapsedTime = 0.f;

	// HeterogeneousVolume/BinaryManager가 참조할 화재 프레임 번호입니다.
	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	int32 FireFrame = 0;

	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	int32 EvacuatedCount = 0;

	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	int32 IncapacitatedCount = 0;

	UPROPERTY(BlueprintReadOnly, Category="YUFS|Timeline")
	TArray<FYUFSTimelineNPCSnapshot> NPCSnapshots;
};
