// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/Perception/YUFSNPCPerceptionComponent.h"

#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Fire/YUFSBinaryManager.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Math/RotationMatrix.h"

UYUFSNPCPerceptionComponent::UYUFSNPCPerceptionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UYUFSNPCPerceptionComponent::BeginPlay()
{
	Super::BeginPlay();

	for (TActorIterator<AYUFSBinaryManager> It(GetWorld()); It; ++It)
	{
		BinaryManager = *It;
		break;
	}
}

void UYUFSNPCPerceptionComponent::UpdatePerception(int32 CurrentFrame)
{
	if (!BinaryManager || !Config || !GetOwner())
	{
		CachedSmokeDensity = 0.f;
		CachedTemperature = 0.f;
		CachedSmokeInFrontNormalized = 0.f;
		CachedSmokeAboveNormalized = 0.f;
		CachedRiskLevel = 0.f;
		return;
	}

	const FVector MyPos = GetOwner()->GetActorLocation();

	uint8 RawDensity = 0;
	if (BinaryManager->GetSmokeDensityAtLocation(MyPos, CurrentFrame, RawDensity))
	{
		CachedSmokeDensity = FMath::Clamp(RawDensity / 255.f, 0.f, 1.f);
	}
	else
	{
		CachedSmokeDensity = 0.f;
	}

	uint8 RawTemp = 0;
	if (BinaryManager->GetTemperatureAtLocation(MyPos, CurrentFrame, RawTemp))
	{
		CachedTemperature = FMath::Clamp(RawTemp / 255.f, 0.f, 1.f);
	}
	else
	{
		CachedTemperature = 0.f;
	}

	CachedSmokeInFrontNormalized = 0.f;
	CachedSmokeAboveNormalized = 0.f;

	APawn* PawnOwner = Cast<APawn>(GetOwner());
	const FVector ViewOrigin = PawnOwner ? PawnOwner->GetPawnViewLocation() : MyPos;
	FRotator BaseFacing = GetOwner()->GetActorRotation();
	BaseFacing.Pitch = 0.f;
	BaseFacing.Roll = 0.f;

	const int32 SafeFrontRayCount = FMath::Max(1, Config->VisionRayCount);
	const int32 SafeUpperYawRayCount = FMath::Max(1, Config->UpperVisionYawRayCount);
	const int32 SafeSampleCount = FMath::Max(1, Config->VisionSamplesPerRay);
	const float FrontHalfFOV = Config->FieldOfViewDegrees * 0.5f;
	const float FrontSampleStep = Config->VisionRange / static_cast<float>(SafeSampleCount);
	const float OverheadSampleStep = Config->OverheadProbeRange / static_cast<float>(SafeSampleCount);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(YUFSVisionTrace), false, GetOwner());

	auto ScanRay = [this, &QueryParams, &ViewOrigin, CurrentFrame, SafeSampleCount](
		const FVector& RayDirection,
		float Range,
		float SampleStep,
		float& Accumulator,
		bool bDrawDebug) -> void
	{
		const FVector RayEnd = ViewOrigin + (RayDirection * Range);

		FHitResult Hit;
		const bool bHit = GetWorld()->LineTraceSingleByChannel(
			Hit,
			ViewOrigin,
			RayEnd,
			ECC_Visibility,
			QueryParams);

		const float VisibleDistance = bHit
			? FMath::Max(0.f, Hit.Distance - Config->OcclusionSampleMarginCm)
			: Range;

		float RayMaxSmoke = 0.f;
		for (int32 SampleIndex = 1; SampleIndex <= SafeSampleCount; ++SampleIndex)
		{
			const float SampleDistance = SampleStep * static_cast<float>(SampleIndex);
			if (SampleDistance > VisibleDistance)
			{
				break;
			}

			const FVector SamplePoint = ViewOrigin + (RayDirection * SampleDistance);
			RayMaxSmoke = FMath::Max(RayMaxSmoke, SampleSmokeAtPoint(SamplePoint, CurrentFrame));
		}

		Accumulator = FMath::Max(Accumulator, RayMaxSmoke);

		if (bDrawDebug)
		{
			const FVector DebugEnd = bHit ? Hit.ImpactPoint : RayEnd;
			const FColor RayColor = FLinearColor::LerpUsingHSV(FLinearColor::Green, FLinearColor::Red, RayMaxSmoke).ToFColor(true);
			DrawDebugLine(GetWorld(), ViewOrigin, DebugEnd, RayColor, false, 0.f, 0, 1.5f);

			if (bHit)
			{
				DrawDebugPoint(GetWorld(), Hit.ImpactPoint, 8.f, FColor::White, false, 0.f);
			}
		}
	};

	for (int32 RayIndex = 0; RayIndex < SafeFrontRayCount; ++RayIndex)
	{
		const float Alpha = SafeFrontRayCount == 1
			? 0.f
			: static_cast<float>(RayIndex) / static_cast<float>(SafeFrontRayCount - 1);
		const float YawOffset = FMath::Lerp(-FrontHalfFOV, FrontHalfFOV, Alpha);
		const FVector RayDirection = FRotationMatrix(BaseFacing + FRotator(0.f, YawOffset, 0.f)).GetUnitAxis(EAxis::X);
		ScanRay(RayDirection, Config->VisionRange, FrontSampleStep, CachedSmokeInFrontNormalized, Config->bDrawVisionDebug);
	}

	const float UpperHalfYaw = Config->UpperVisionYawHalfSpreadDegrees;
	const float UpperPitches[] = {Config->UpperVisionPitchLowDegrees, Config->UpperVisionPitchHighDegrees};
	for (const float UpperPitch : UpperPitches)
	{
		for (int32 RayIndex = 0; RayIndex < SafeUpperYawRayCount; ++RayIndex)
		{
			const float Alpha = SafeUpperYawRayCount == 1
				? 0.f
				: static_cast<float>(RayIndex) / static_cast<float>(SafeUpperYawRayCount - 1);
			const float YawOffset = FMath::Lerp(-UpperHalfYaw, UpperHalfYaw, Alpha);
			const FVector RayDirection = FRotationMatrix(BaseFacing + FRotator(UpperPitch, YawOffset, 0.f)).GetUnitAxis(EAxis::X);
			ScanRay(RayDirection, Config->VisionRange, FrontSampleStep, CachedSmokeAboveNormalized, Config->bDrawVisionDebug);
		}
	}

	const FVector OverheadDirection =
		FRotationMatrix(BaseFacing + FRotator(Config->OverheadProbePitchDegrees, 0.f, 0.f)).GetUnitAxis(EAxis::X);
	ScanRay(
		OverheadDirection,
		Config->OverheadProbeRange,
		OverheadSampleStep,
		CachedSmokeAboveNormalized,
		Config->bDrawVisionDebug);

	CachedRiskLevel = ComputeRiskLevel(CachedSmokeDensity, CachedTemperature);
}

float UYUFSNPCPerceptionComponent::SampleSmokeAtPoint(FVector WorldPos, int32 Frame) const
{
	if (!BinaryManager)
	{
		return 0.f;
	}

	uint8 RawDensity = 0;
	if (BinaryManager->GetSmokeDensityAtLocation(WorldPos, Frame, RawDensity))
	{
		return FMath::Clamp(RawDensity / 255.f, 0.f, 1.f);
	}

	return 0.f;
}
