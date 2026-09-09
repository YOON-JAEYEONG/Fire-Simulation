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

// ── 시뮬레이션 시작 시 배정하는 경로 선택 성향 ─────────────────────────
// 보고서의 70:20:10은 매 정책 Tick마다 다시 추첨하는 확률이 아니라
// 전체 NPC 집단에 한 번 배정하는 보정 목표다. 실제 경로가 위험하거나
// 후보가 없으면 이 성향보다 안전 폴백이 우선한다.
UENUM(BlueprintType)
enum class EYUFSRoutePreference : uint8
{
	FamiliarExit,       // 70%: 친숙한 출구
	SocialFollowing,    // 20%: 군중/리더 추종
	NearestSafeExit     // 10%: 표지 또는 최근접 안전 출구
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

UENUM(BlueprintType)
enum class EYUFSTerminalReason : uint8
{
	None,
	ReachedExit,
	Incapacitated,
	TimedOut
};
