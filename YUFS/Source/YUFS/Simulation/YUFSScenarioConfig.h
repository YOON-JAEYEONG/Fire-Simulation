// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "YUFSScenarioConfig.generated.h"

class UWorld;
class UTexture2D;

// ── NPC 의사결정 정책 ─────────────────────────────────────────────────────
// FYUFSPolicyFactory::EPolicyType 과 1:1로 매핑됩니다. (그 enum은 BlueprintType이
// 아니라서 UI/설정 구조체에서 직접 쓸 수 없으므로 별도로 둡니다.)
UENUM(BlueprintType)
enum class EYUFSScenarioPolicy : uint8
{
	RuleBased UMETA(DisplayName = "규칙 기반"),
	ML        UMETA(DisplayName = "ML (ONNX)")
};

// ── NPC 초기 배치 방식 ───────────────────────────────────────────────────
UENUM(BlueprintType)
enum class EYUFSPlacementPreset : uint8
{
	UseLevelPlacement UMETA(DisplayName = "레벨에 배치된 그대로"),
	Uniform           UMETA(DisplayName = "균등 분산"),
	Clustered         UMETA(DisplayName = "군집"),
	NearExits         UMETA(DisplayName = "출구 인근 밀집")
};

// ── 시나리오 설정 화면에서 고를 수 있는 맵 항목 ──────────────────────────
USTRUCT(BlueprintType)
struct FYUFSScenarioMapOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FText DisplayName;

	// 화재 데이터가 준비된 시뮬레이션 레벨
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map", meta = (AllowedClasses = "/Script/Engine.World"))
	TSoftObjectPtr<UWorld> Map;

	// 메뉴 카드에 표시할 썸네일 (선택)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	TObjectPtr<UTexture2D> Thumbnail = nullptr;

	// 층/규모 등 부가 설명 (선택)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FText Description;
};

// ── 한 번의 실험(배치)을 정의하는 전체 설정 ─────────────────────────────
// 메인 메뉴 → GameInstance → SimulationController 로 전달됩니다.
USTRUCT(BlueprintType)
struct YUFS_API FYUFSScenarioConfig
{
	GENERATED_BODY()

	// ── 대상 맵 ──────────────────────────────────────────────────────────
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario|Map", meta = (AllowedClasses = "/Script/Engine.World"))
	TSoftObjectPtr<UWorld> SimulationMap;

	// 결과/기록에 남길 사람이 읽을 수 있는 이름
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario|Map")
	FText DisplayName;

	// ── NPC ─────────────────────────────────────────────────────────────
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario|NPC", meta = (ClampMin = "0"))
	int32 NPCCount = 20;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario|NPC")
	EYUFSPlacementPreset PlacementPreset = EYUFSPlacementPreset::UseLevelPlacement;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario|NPC")
	EYUFSScenarioPolicy PolicyType = EYUFSScenarioPolicy::RuleBased;

	// PolicyType == ML 일 때 사용할 ONNX 모델 경로 (프로젝트 상대 또는 절대)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario|NPC")
	FString OnnxModelPath;

	// ── 화재 / 시간 ─────────────────────────────────────────────────────
	// 레벨 시작 후 불이 붙기까지의 대기 시간 (NPC 일상 상태 구간)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario|Fire", meta = (ClampMin = "0.0"))
	float FireStartDelaySeconds = 30.f;

	// 이 시간이 지나면 회차를 강제 종료
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario|Fire", meta = (ClampMin = "0.0"))
	float MaxSimDurationSeconds = 300.f;

	// ── 비상 통신 이벤트 ────────────────────────────────────────────────
	// 모두 "화재 시작" 기준 오프셋(초). 음수이면 해당 이벤트 비활성.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario|Comm")
	float AlarmTriggerOffsetSeconds = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario|Comm")
	float PreRecordedMsgOffsetSeconds = -1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario|Comm")
	float LiveAnnouncementOffsetSeconds = -1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario|Comm")
	float StaffGuidanceOffsetSeconds = -1.f;

	// ── 타임라인 기록 ───────────────────────────────────────────────────
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario|Timeline")
	bool bEnableTimelineRecording = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario|Timeline", meta = (ClampMin = "0.0"))
	float TimelineRecordEndFireSeconds = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario|Timeline", meta = (ClampMin = "0.05"))
	float TimelineRecordIntervalSeconds = 0.25f;

	// 맵이 지정되어 있고 값들이 정상 범위인지 확인합니다.
	bool IsValid(FText& OutError) const;
};
