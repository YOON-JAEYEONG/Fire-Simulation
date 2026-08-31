// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "YUFSTypes.generated.h"

// ── PADM 이론 기반 행동 상태 ──────────────────────────────────────────
// 논문(Kuligowski): Pre-evacuation → Movement 두 단계를
// 실제 하위 페이즈로 세분화한 것
UENUM(BlueprintType)
enum class EYUFSBehaviorState : uint8
{
	Normal,          // 일상 상태
	Perceiving,      // 단서 감지 (연기·경보 등)
	Milling,         // 사태 파악·정보 탐색 (PADM Milling Phase)
	RiskAssessment,  // 개인 위험 판단 (PADM Risk Assessment)
	Preparing,       // 대피 준비 (소지품·타인 알림 등)
	Evacuating,      // 실제 대피 이동
	Helping,         // 타인 도움 (Altruistic Behavior)
	Crawling,        // 기어가기 (연기 흡입 누적 CrawlThreshold~IncapacitationThreshold 구간)
	Incapacitated    // 행동 불능 (연기 흡입 임계값 초과)
};

// ── NPC가 선택 가능한 구체적 행동 ────────────────────────────────────
// IYUFSDecisionPolicy::SelectAction()의 반환 타입
UENUM(BlueprintType)
enum class EYUFSAction : uint8
{
	Idle,
	SeekInformation,          // 주변 두리번 (Milling 세부 행동)
	AlertNearbyOccupants,     // 옆 NPC에게 위험 알림
	GatherBelongings,         // 소지품 챙기기 (연구: 평균 0.5~5분 지연)
	EvacuateToNearestExit,    // 가장 가까운 출구로
	EvacuateToFamiliarExit,   // 친숙한 출구로 (Affiliative Model)
	HelpOther,                // 느린 NPC 동행
	WaitForInfo,              // 정보 대기
	Cough,                    // 연기 흡입 반응 (애니메이션 트리거용)
	FollowCrowd,              // 군중 휩쓸리기 (Herd Instinct)
	Film                      // 촬영 행동 — van der Wal: 경보만 있을 때 56%로 가장 빈번한 지연 행동
};

// ── 근거 기반 결정 계층 ──────────────────────────────────────────────
// 기존 BehaviorState/Action 숫자는 ONNX V1 계약을 위해 그대로 유지한다.
// Intent와 ActionTask는 병렬 계층으로 추가하며 기존 상태는 UI 호환 투영에 사용한다.
UENUM(BlueprintType)
enum class EYUFSIntent : uint8
{
	Observe,
	Prepare,
	CommitEvac,
	Help,
	Shelter,
	Reenter,
	Incapacitated
};

UENUM(BlueprintType)
enum class EYUFSActionTask : uint8
{
	None,
	ContinueRoutine,
	ObserveOthers,
	SeekInformation,
	AlertHelp,
	GatherBelongings,
	AssistOther,
	WaitForOfficialInfo,
	FilmObserve,
	InitialExtinguish,
	Freeze
};

UENUM(BlueprintType)
enum class EYUFSTaskCancelReason : uint8
{
	None,
	Completed,
	LifeRisk,
	OfficialInstruction,
	IntentChanged,
	InvalidContext,
	Timeout
};

UENUM(BlueprintType)
enum class EYUFSTerminalReason : uint8
{
	None,
	ReachedExit,
	Incapacitated,
	TimedOut
};
