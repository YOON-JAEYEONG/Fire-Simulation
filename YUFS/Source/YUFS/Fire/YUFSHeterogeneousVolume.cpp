// Fill out your copyright notice in the Description page of Project Settings.

#include "YUFSHeterogeneousVolume.h"
#include "Misc/ConfigCacheIni.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"

AYUFSHeterogeneousVolume::AYUFSHeterogeneousVolume()
{
	PrimaryActorTick.bCanEverTick = true;

	HeterogeneousVolumeComponent = CreateDefaultSubobject<UHeterogeneousVolumeComponent>(TEXT("YUFSHeterogeneousVolumeComponent"));
	// 생성자에서는 재생하지 않음 — SimulationController가 제어
	HeterogeneousVolumeComponent->EndFrame = 0.f;
	HeterogeneousVolumeComponent->bPlaying = false;
}

void AYUFSHeterogeneousVolume::BeginPlay()
{
	Super::BeginPlay();
	// User-marked second-floor room in Prototype. Store world coordinates in config,
	// then convert through the existing volume's scaled/reflected transform.
	FVector AuthoredWorldTarget;
	if (!bHasInteractionTarget && GetWorld()->GetMapName().EndsWith(TEXT("Prototype"))
		&& GConfig->GetVector(TEXT("YUFS.PrototypeFireTarget"), TEXT("WorldLocation"), AuthoredWorldTarget, GGameIni))
	{
		InteractionTargetLocal = GetActorTransform().InverseTransformPosition(AuthoredWorldTarget);
		bHasInteractionTarget = true;
		UE_LOG(LogTemp, Display, TEXT("[NPCSuppression] User-marked ignition target: %s"), *AuthoredWorldTarget.ToString());
	}

	// 설정 값으로 컴포넌트 초기화 (재생은 하지 않음)
	CreateLocalEffects();
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
	bLocalFireActive = true; bLocalEffectsPlaying = true; LocalFireStrength = 1.f; LocalEffectTime = 0.f;
	RefreshLocalEffects();
	if (!HeterogeneousVolumeComponent) return;

	HeterogeneousVolumeComponent->Frame = 0.f;
	HeterogeneousVolumeComponent->bPlaying = true;
	UE_LOG(LogTemp, Warning, TEXT("[YUFSFire] 🔥 Fire STARTED. Frame reset to 0."));
}

void AYUFSHeterogeneousVolume::PauseFire()
{
	bLocalEffectsPlaying = false;
	if (!HeterogeneousVolumeComponent) return;

	HeterogeneousVolumeComponent->bPlaying = false;
	UE_LOG(LogTemp, Log, TEXT("[YUFSFire] Fire PAUSED at frame %.0f."),
		HeterogeneousVolumeComponent->Frame);
}

void AYUFSHeterogeneousVolume::ResumeFire()
{
	bLocalEffectsPlaying = true;
	if (!HeterogeneousVolumeComponent) return;

	HeterogeneousVolumeComponent->bPlaying = true;
	UE_LOG(LogTemp, Log, TEXT("[YUFSFire] Fire RESUMED from frame %.0f."),
		HeterogeneousVolumeComponent->Frame);
}

void AYUFSHeterogeneousVolume::ResetFire()
{
	bLocalFireActive = false; bLocalEffectsPlaying = false; LocalFireStrength = 1.f; LocalEffectTime = 0.f;
	RefreshLocalEffects();
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

void AYUFSHeterogeneousVolume::SetFrame(int32 TargetFrame)
{
	if (!HeterogeneousVolumeComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("[YUFSFire] SetFrame failed: HeterogeneousVolumeComponent is null."));
		return;
	}

	// TotalFrameCount는 float로 관리되고 있으므로 안전하게 int 범위로 Clamp합니다.
	const int32 MaxFrame = FMath::Max(0, FMath::FloorToInt(TotalFrameCount) - 1);
	const int32 ClampedFrame = FMath::Clamp(TargetFrame, 0, MaxFrame);

	HeterogeneousVolumeComponent->Frame = static_cast<float>(ClampedFrame);
	HeterogeneousVolumeComponent->EndFrame = TotalFrameCount;

	// Seek는 "이 시점으로 이동"하는 동작이므로 기본적으로 정지 상태로 둡니다.
	// 재생 버튼을 누르면 TimelineRecorder가 다시 ResumeFire()를 호출합니다.
	HeterogeneousVolumeComponent->bPlaying = false;
}

bool AYUFSHeterogeneousVolume::IsPlaying() const
{
	return HeterogeneousVolumeComponent && HeterogeneousVolumeComponent->bPlaying;
}

bool AYUFSHeterogeneousVolume::GetInteractionTarget(FVector& OutWorldLocation) const
{
	OutWorldLocation = FVector::ZeroVector;
	if (!bHasInteractionTarget) return false;
	OutWorldLocation = GetActorTransform().TransformPosition(InteractionTargetLocal);
	return !OutWorldLocation.ContainsNaN();
}

void AYUFSHeterogeneousVolume::CreateLocalEffects()
{
	FVector Target;
	if (!GetInteractionTarget(Target) || !LocalEffectPlanes.IsEmpty()) return;
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	for (int32 I = 0; I < 5; ++I)
	{
		const bool bSmoke = I >= 3;
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, bSmoke
			? TEXT("/Game/Effects/LocalFire/M_LocalSmoke.M_LocalSmoke")
			: TEXT("/Game/Effects/LocalFire/M_LocalFlame.M_LocalFlame"));
		if (!Material || !Plane) continue;
		auto* Effect = NewObject<UStaticMeshComponent>(this);
		AddInstanceComponent(Effect);
		Effect->SetStaticMesh(Plane);
		Effect->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Effect->SetCanEverAffectNavigation(false);
		Effect->SetCastShadow(false);
		Effect->RegisterComponent();
		Effect->SetWorldLocation(Target + FVector(0, 0, bSmoke ? 220.f : 85.f));
		Effect->SetWorldRotation(FRotator(0, I * 60.f, 90.f));
		Effect->SetWorldScale3D(bSmoke ? FVector(2.1f, 2.6f, 1) : FVector(1.0f, 1.7f, 1));
		Effect->SetMaterial(0, Material);
		LocalEffectMaterials.Add(Effect->CreateAndSetMaterialInstanceDynamic(0));
		LocalEffectPlanes.Add(Effect);
	}
	LocalFireLight = NewObject<UPointLightComponent>(this);
	AddInstanceComponent(LocalFireLight);
	LocalFireLight->RegisterComponent();
	LocalFireLight->SetWorldLocation(Target + FVector(0, 0, 85));
	LocalFireLight->SetLightColor(FLinearColor(1.f, 0.22f, 0.02f));
	LocalFireLight->SetAttenuationRadius(650.f);
	LocalFireLight->SetCastShadows(false);
	RefreshLocalEffects();
}

void AYUFSHeterogeneousVolume::RefreshLocalEffects()
{
	const bool bVisible = IsLocalFireBurning();
	for (const auto& Effect : LocalEffectPlanes) if (Effect) Effect->SetVisibility(bVisible);
	for (const auto& Material : LocalEffectMaterials) if (Material)
	{
		Material->SetScalarParameterValue(TEXT("Phase"), LocalEffectTime);
		Material->SetScalarParameterValue(TEXT("Strength"), LocalFireStrength);
	}
	if (LocalFireLight)
	{
		LocalFireLight->SetVisibility(bVisible);
		LocalFireLight->SetIntensity(7000.f * LocalFireStrength * (0.88f + 0.12f * FMath::Sin(LocalEffectTime * 13.f)));
	}
}

void AYUFSHeterogeneousVolume::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bLocalEffectsPlaying) LocalEffectTime += DeltaSeconds;
	RefreshLocalEffects();
}

void AYUFSHeterogeneousVolume::ApplyLocalSuppression(float Amount)
{
	if (!bLocalFireActive) return;
	LocalFireStrength = FMath::Max(0.f, LocalFireStrength - FMath::Max(0.f, Amount));
	if (LocalFireStrength <= KINDA_SMALL_NUMBER) LocalFireStrength = 0.f;
	RefreshLocalEffects();
}
