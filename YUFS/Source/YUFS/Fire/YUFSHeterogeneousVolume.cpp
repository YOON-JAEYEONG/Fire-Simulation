// Fill out your copyright notice in the Description page of Project Settings.

#include "YUFSHeterogeneousVolume.h"

AYUFSHeterogeneousVolume::AYUFSHeterogeneousVolume()
{
	PrimaryActorTick.bCanEverTick = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// FireScenarios 배열 크기에 맞게 VolumeComponent를 동기화
// 에디터에서 FireScenarios 항목을 추가/제거하면 자동 호출됨
// ─────────────────────────────────────────────────────────────────────────────

void AYUFSHeterogeneousVolume::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	const int32 Needed = FireScenarios.Num();

	// 무효화된 포인터 정리
	VolumeComponents.RemoveAll([](const TObjectPtr<UHeterogeneousVolumeComponent>& C)
	{
		return !IsValid(C);
	});

	// 부족하면 생성
	while (VolumeComponents.Num() < Needed)
	{
		const int32 Idx = VolumeComponents.Num();
		FName Name = *FString::Printf(TEXT("VolumeComponent_%d"), Idx);

		UHeterogeneousVolumeComponent* VC = NewObject<UHeterogeneousVolumeComponent>(this, Name);
		VC->SetupAttachment(GetRootComponent());
		VC->SetVisibility(false);
		VC->bPlaying = false;
		VC->RegisterComponent();
		AddInstanceComponent(VC);

		VolumeComponents.Add(VC);
		UE_LOG(LogTemp, Log, TEXT("[YUFSFire] OnConstruction: Created %s"), *Name.ToString());
	}

	// 넘치면 숨김 (삭제하지 않음: 위치 정보 보존)
	for (int32 i = Needed; i < VolumeComponents.Num(); i++)
	{
		if (IsValid(VolumeComponents[i]))
			VolumeComponents[i]->SetVisibility(false);
	}
}

void AYUFSHeterogeneousVolume::BeginPlay()
{
	Super::BeginPlay();

	for (auto& VC : VolumeComponents)
		if (IsValid(VC)) VC->SetVisibility(false);

	SetActiveScenario(ActiveScenarioIndex);

	if (bAutoPlayOnBeginPlay)
	{
		if (UHeterogeneousVolumeComponent* VC = GetActiveVolumeComponent())
		{
			VC->bPlaying = true;
			UE_LOG(LogTemp, Warning, TEXT("[YUFSFire] Auto-play enabled. Fire starts immediately."));
		}
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[YUFSFire] Standby. Waiting for SimulationController to call StartFire()."));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 내부 헬퍼
// ─────────────────────────────────────────────────────────────────────────────

UHeterogeneousVolumeComponent* AYUFSHeterogeneousVolume::GetActiveVolumeComponent() const
{
	if (VolumeComponents.IsValidIndex(ActiveScenarioIndex) && IsValid(VolumeComponents[ActiveScenarioIndex]))
		return VolumeComponents[ActiveScenarioIndex].Get();
	return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// 시나리오 전환
// ─────────────────────────────────────────────────────────────────────────────

void AYUFSHeterogeneousVolume::SetActiveScenario(int32 Index)
{
	if (!FireScenarios.IsValidIndex(Index))
	{
		UE_LOG(LogTemp, Error, TEXT("[YUFSFire] SetActiveScenario: Index %d out of range (%d scenarios defined)."),
			Index, FireScenarios.Num());
		return;
	}

	// 이전 활성 컴포넌트 숨김
	if (UHeterogeneousVolumeComponent* Prev = GetActiveVolumeComponent())
		Prev->SetVisibility(false);

	ActiveScenarioIndex = Index;
	const FFireScenarioConfig& Config = FireScenarios[Index];

	UHeterogeneousVolumeComponent* VC = GetActiveVolumeComponent();
	if (!VC)
	{
		UE_LOG(LogTemp, Error, TEXT("[YUFSFire] SetActiveScenario: VolumeComponent_%d가 없습니다. OnConstruction이 실행되지 않았을 수 있습니다."), Index);
		return;
	}

	// 머티리얼 교체
	if (!Config.SparseMaterial.IsNull())
	{
		if (UMaterialInterface* Mat = Config.SparseMaterial.LoadSynchronous())
			VC->SetMaterial(0, Mat);
		else
			UE_LOG(LogTemp, Warning, TEXT("[YUFSFire] SetActiveScenario: Failed to load material for scenario %d."), Index);
	}

	// 프레임 파라미터 초기화
	VC->Frame = 0.f;
	VC->FrameRate = Config.PlaybackFrameRate;
	VC->EndFrame = Config.TotalFrameCount;
	VC->bPlaying = false;
	VC->SetVisibility(true);

	UE_LOG(LogTemp, Log, TEXT("[YUFSFire] Active scenario -> [%d] | Binary: %s | Origin: %s"),
		Index, *Config.BinaryDataPath, *Config.WorldOrigin.ToString());
}

FString AYUFSHeterogeneousVolume::GetActiveScenarioBinaryPath() const
{
	if (FireScenarios.IsValidIndex(ActiveScenarioIndex))
		return FireScenarios[ActiveScenarioIndex].BinaryDataPath;
	return FString();
}

FVector AYUFSHeterogeneousVolume::GetActiveScenarioWorldOrigin() const
{
	if (FireScenarios.IsValidIndex(ActiveScenarioIndex))
		return FireScenarios[ActiveScenarioIndex].WorldOrigin;
	return FVector::ZeroVector;
}

// ─────────────────────────────────────────────────────────────────────────────
// 제어 API
// ─────────────────────────────────────────────────────────────────────────────

void AYUFSHeterogeneousVolume::StartFire()
{
	UHeterogeneousVolumeComponent* VC = GetActiveVolumeComponent();
	if (!VC) return;

	VC->Frame = 0.f;
	VC->bPlaying = true;
	UE_LOG(LogTemp, Warning, TEXT("[YUFSFire] Fire STARTED. Frame reset to 0."));
}

void AYUFSHeterogeneousVolume::PauseFire()
{
	UHeterogeneousVolumeComponent* VC = GetActiveVolumeComponent();
	if (!VC) return;

	VC->bPlaying = false;
	UE_LOG(LogTemp, Log, TEXT("[YUFSFire] Fire PAUSED at frame %.0f."), VC->Frame);
}

void AYUFSHeterogeneousVolume::ResumeFire()
{
	UHeterogeneousVolumeComponent* VC = GetActiveVolumeComponent();
	if (!VC) return;

	VC->bPlaying = true;
	UE_LOG(LogTemp, Log, TEXT("[YUFSFire] Fire RESUMED from frame %.0f."), VC->Frame);
}

void AYUFSHeterogeneousVolume::ResetFire()
{
	UHeterogeneousVolumeComponent* VC = GetActiveVolumeComponent();
	if (!VC) return;

	VC->Frame = 0.f;
	VC->bPlaying = false;
	UE_LOG(LogTemp, Log, TEXT("[YUFSFire] Fire RESET to frame 0."));
}

int32 AYUFSHeterogeneousVolume::GetFrame() const
{
	if (UHeterogeneousVolumeComponent* VC = GetActiveVolumeComponent())
		return static_cast<int32>(VC->Frame);

	UE_LOG(LogTemp, Error, TEXT("[YUFSFire] GetFrame: 활성 VolumeComponent가 없습니다."));
	return 0;
}

bool AYUFSHeterogeneousVolume::IsPlaying() const
{
	UHeterogeneousVolumeComponent* VC = GetActiveVolumeComponent();
	return VC && VC->bPlaying;
}
