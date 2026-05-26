// Fill out your copyright notice in the Description page of Project Settings.

#include "YUFSHeterogeneousVolume.h"

AYUFSHeterogeneousVolume::AYUFSHeterogeneousVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	HeterogeneousVolumeComponent = CreateDefaultSubobject<UHeterogeneousVolumeComponent>(TEXT("YUFSHeterogeneousVolumeComponent"));
	// 생성자에서는 재생하지 않음 — SimulationController가 제어
	HeterogeneousVolumeComponent->EndFrame = 0.f;
	HeterogeneousVolumeComponent->bPlaying = false;
}

void AYUFSHeterogeneousVolume::BeginPlay()
{
	Super::BeginPlay();

	// 설정 값으로 컴포넌트 초기화 (재생은 하지 않음)
	if (HeterogeneousVolumeComponent)
	{
		HeterogeneousVolumeComponent->Frame = 0.f;
		HeterogeneousVolumeComponent->FrameRate = PlaybackFrameRate;
		HeterogeneousVolumeComponent->EndFrame = TotalFrameCount;
		HeterogeneousVolumeComponent->bPlaying = false;

		// 에디터 테스트용: bAutoPlayOnBeginPlay가 true이면 즉시 재생
		if (bAutoPlayOnBeginPlay)
		{
			HeterogeneousVolumeComponent->bPlaying = true;
			UE_LOG(LogTemp, Warning, TEXT("[YUFSFire] Auto-play enabled. Fire starts immediately."));
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("[YUFSFire] Standby. Waiting for SimulationController to call StartFire()."));
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 제어 API
// ─────────────────────────────────────────────────────────────────────────────

void AYUFSHeterogeneousVolume::StartFire()
{
	if (!HeterogeneousVolumeComponent) return;

	HeterogeneousVolumeComponent->Frame = 0.f;
	HeterogeneousVolumeComponent->bPlaying = true;
	UE_LOG(LogTemp, Warning, TEXT("[YUFSFire] 🔥 Fire STARTED. Frame reset to 0."));
}

void AYUFSHeterogeneousVolume::PauseFire()
{
	if (!HeterogeneousVolumeComponent) return;

	HeterogeneousVolumeComponent->bPlaying = false;
	UE_LOG(LogTemp, Log, TEXT("[YUFSFire] Fire PAUSED at frame %.0f."),
		HeterogeneousVolumeComponent->Frame);
}

void AYUFSHeterogeneousVolume::ResumeFire()
{
	if (!HeterogeneousVolumeComponent) return;

	HeterogeneousVolumeComponent->bPlaying = true;
	UE_LOG(LogTemp, Log, TEXT("[YUFSFire] Fire RESUMED from frame %.0f."),
		HeterogeneousVolumeComponent->Frame);
}

void AYUFSHeterogeneousVolume::ResetFire()
{
	if (!HeterogeneousVolumeComponent) return;

	HeterogeneousVolumeComponent->Frame = 0.f;
	HeterogeneousVolumeComponent->bPlaying = false;
	UE_LOG(LogTemp, Log, TEXT("[YUFSFire] Fire RESET to frame 0."));
}

int32 AYUFSHeterogeneousVolume::GetFrame() const
{
	if (HeterogeneousVolumeComponent)
	{
		return static_cast<int32>(HeterogeneousVolumeComponent->Frame);
	}
	UE_LOG(LogTemp, Error, TEXT("[YUFSFire] HeterogeneousVolumeComponent가 비어있습니다!"));
	return 0;
}

bool AYUFSHeterogeneousVolume::IsPlaying() const
{
	return HeterogeneousVolumeComponent && HeterogeneousVolumeComponent->bPlaying;
}
