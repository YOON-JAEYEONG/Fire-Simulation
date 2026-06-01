// Fill out your copyright notice in the Description page of Project Settings.

#include "Simulation/YUFSSimHUD.h"

#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

void UYUFSSimHUD::NativeConstruct()
{
	Super::NativeConstruct();
	FindSimController();
}

void UYUFSSimHUD::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 컨트롤러가 없으면 매 틱마다 재탐색
	if (!SimController)
	{
		FindSimController();
	}
}

void UYUFSSimHUD::FindSimController()
{
	if (!GetWorld()) return;
	for (TActorIterator<AYUFSSimulationController> It(GetWorld()); It; ++It)
	{
		SimController = *It;
		break;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 버튼 핸들러
// ─────────────────────────────────────────────────────────────────────────────

void UYUFSSimHUD::OnStartButtonClicked()
{
	if (!SimController) return;

	const ESimPhase Phase = SimController->GetCurrentPhase();
	if (Phase == ESimPhase::WaitingToStart)
	{
		SimController->StartSimulation();
	}
	else
	{
		SimController->ResumeSimulation();
	}
}

void UYUFSSimHUD::OnPauseButtonClicked()
{
	if (SimController) SimController->PauseSimulation();
}

void UYUFSSimHUD::OnStopButtonClicked()
{
	if (SimController) SimController->StopAndResetSimulation();
}

// ─────────────────────────────────────────────────────────────────────────────
// 데이터 조회 (UMG 바인딩용)
// ─────────────────────────────────────────────────────────────────────────────

FText UYUFSSimHUD::GetPhaseText() const
{
	if (!SimController) return FText::FromString(TEXT("--"));

	switch (SimController->GetCurrentPhase())
	{
	case ESimPhase::WaitingToStart: return FText::FromString(TEXT("대기 중"));
	case ESimPhase::FireStartDelay: return FText::FromString(TEXT("화재 발생 전"));
	case ESimPhase::FireActive:     return FText::FromString(TEXT("🔥 화재 진행 중"));
	case ESimPhase::TimelineReview: return FText::FromString(TEXT("타임라인 관찰 모드"));
	case ESimPhase::Completed:      return FText::FromString(TEXT("✅ 시뮬레이션 완료"));
	default:                        return FText::FromString(TEXT("--"));
	}
}

FText UYUFSSimHUD::GetElapsedTimeText() const
{
	if (!SimController) return FText::FromString(TEXT("0:00"));

	const float Elapsed = SimController->GetElapsedTime();
	const int32 Minutes = FMath::FloorToInt(Elapsed / 60.f);
	const int32 Seconds = FMath::FloorToInt(FMath::Fmod(Elapsed, 60.f));
	return FText::FromString(FString::Printf(TEXT("%d:%02d"), Minutes, Seconds));
}

FText UYUFSSimHUD::GetFireCountdownText() const
{
	if (!SimController) return FText::FromString(TEXT(""));

	const float Countdown = SimController->GetFireStartCountdown();
	if (Countdown <= 0.f) return FText::FromString(TEXT(""));
	return FText::FromString(FString::Printf(TEXT("화재까지 %.0f초"), Countdown));
}

FText UYUFSSimHUD::GetNPCStatusText() const
{
	if (!SimController) return FText::FromString(TEXT("NPC: -/-"));

	return FText::FromString(FString::Printf(
		TEXT("대피: %d | 사망: %d | 전체: %d"),
		SimController->GetEvacuatedCount(),
		SimController->GetIncapacitatedCount(),
		SimController->GetTotalNPCCount()));
}

FText UYUFSSimHUD::GetRunProgressText() const
{
	if (!SimController) return FText::FromString(TEXT(""));

	const int32 Current = SimController->GetCurrentRunIndex();
	// TotalRunCount는 공개 프로퍼티이므로 직접 접근
	return FText::FromString(FString::Printf(TEXT("실험 %d회차"), Current));
}

float UYUFSSimHUD::GetEvacuationRatePercent() const
{
	if (!SimController) return 0.f;

	const int32 Total = SimController->GetTotalNPCCount();
	if (Total == 0) return 0.f;

	return (float)SimController->GetEvacuatedCount() / (float)Total * 100.f;
}

bool UYUFSSimHUD::IsSimulationActive() const
{
	if (!SimController) return false;
	const ESimPhase Phase = SimController->GetCurrentPhase();
	return Phase == ESimPhase::FireStartDelay || Phase == ESimPhase::FireActive;
}

bool UYUFSSimHUD::IsFireActive() const
{
	if (!SimController) return false;
	return SimController->GetCurrentPhase() == ESimPhase::FireActive;
}


// ─────────────────────────────────────────────────────────────────────────────
// 타임라인 관찰 모드 바인딩
// ─────────────────────────────────────────────────────────────────────────────

void UYUFSSimHUD::OnTimelinePlayButtonClicked()
{
	if (SimController)
	{
		SimController->PlayTimeline();
	}
}

void UYUFSSimHUD::OnTimelinePauseButtonClicked()
{
	if (SimController)
	{
		SimController->PauseTimeline();
	}
}

void UYUFSSimHUD::OnTimelineSliderChanged(float NormalizedValue)
{
	if (SimController)
	{
		SimController->SeekTimelineByNormalizedValue(NormalizedValue);
	}
}

bool UYUFSSimHUD::IsTimelineReviewMode() const
{
	return SimController && SimController->IsTimelineReviewMode();
}

bool UYUFSSimHUD::IsTimelinePlaying() const
{
	return SimController && SimController->IsTimelinePlaying();
}

float UYUFSSimHUD::GetTimelineProgress01() const
{
	return SimController ? SimController->GetTimelineProgress01() : 0.f;
}

FText UYUFSSimHUD::GetTimelineTimeText() const
{
	if (!SimController)
	{
		return FText::FromString(TEXT("0.0 / 0.0초"));
	}

	return FText::FromString(FString::Printf(
		TEXT("%.1f / %.1f초"),
		SimController->GetTimelineCurrentTime(),
		SimController->GetTimelineMaxTime()));
}
