// Fill out your copyright notice in the Description page of Project Settings.

#include "Debug/YUFSNPCDebugComponent.h"

#include "Core/YUFSObservation.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "NPC/YUFSEvacuationNPC.h"

UYUFSNPCDebugComponent::UYUFSNPCDebugComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UYUFSNPCDebugComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerNPC = Cast<AYUFSEvacuationNPC>(GetOwner());
	SetComponentTickEnabled(OwnerNPC.IsValid());
}

void UYUFSNPCDebugComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!ShouldDraw() || !OwnerNPC.IsValid())
	{
		return;
	}

	DrawDebugOverlay(OwnerNPC->GetLastObservation());
}

bool UYUFSNPCDebugComponent::ShouldDraw() const
{
	if (!bEnabled || !OwnerNPC.IsValid() || !GetWorld())
	{
		return false;
	}

	AActor* OwnerActor = OwnerNPC.Get();
	if (!OwnerActor)
	{
		return false;
	}

	if (bOnlyDrawWhenRecentlyRendered && !OwnerActor->WasRecentlyRendered(RecentRenderToleranceSeconds))
	{
		return false;
	}

	if (MaxDrawDistance > 0.f)
	{
		if (const APlayerController* PlayerController = GetWorld()->GetFirstPlayerController())
		{
			FVector ViewLocation = FVector::ZeroVector;
			FRotator ViewRotation = FRotator::ZeroRotator;
			PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);

			if (FVector::DistSquared(ViewLocation, OwnerActor->GetActorLocation()) > FMath::Square(MaxDrawDistance))
			{
				return false;
			}
		}
	}

	return true;
}

FColor UYUFSNPCDebugComponent::GetRiskColor(float NormalizedRisk) const
{
	const float ClampedRisk = FMath::Clamp(NormalizedRisk, 0.f, 1.f);
	if (ClampedRisk < 0.5f)
	{
		return FLinearColor::LerpUsingHSV(FLinearColor::Green, FLinearColor::Yellow, ClampedRisk * 2.f).ToFColor(true);
	}

	return FLinearColor::LerpUsingHSV(FLinearColor::Yellow, FLinearColor::Red, (ClampedRisk - 0.5f) * 2.f).ToFColor(true);
}

void UYUFSNPCDebugComponent::DrawDebugHemisphere(
	const FVector& Center,
	float Radius,
	bool bUpper,
	const FColor& Color,
	float Thickness) const
{
	if (!GetWorld() || Radius <= 0.f)
	{
		return;
	}

	constexpr int32 LongitudeSegments = 16;
	constexpr int32 LatitudeSegments = 4;
	constexpr int32 ArcSegments = 8;
	const float VerticalSign = bUpper ? 1.f : -1.f;

	// 위도 링: 적도에서 극점 직전까지 반구의 가로 윤곽을 만든다.
	for (int32 LatitudeIndex = 0; LatitudeIndex < LatitudeSegments; ++LatitudeIndex)
	{
		const float Elevation = HALF_PI * static_cast<float>(LatitudeIndex) / static_cast<float>(LatitudeSegments);
		const float RingRadius = Radius * FMath::Cos(Elevation);
		const float Height = VerticalSign * Radius * FMath::Sin(Elevation);

		FVector PreviousPoint = Center + FVector(RingRadius, 0.f, Height);
		for (int32 SegmentIndex = 1; SegmentIndex <= LongitudeSegments; ++SegmentIndex)
		{
			const float Angle = TWO_PI * static_cast<float>(SegmentIndex) / static_cast<float>(LongitudeSegments);
			const FVector CurrentPoint = Center + FVector(
				RingRadius * FMath::Cos(Angle),
				RingRadius * FMath::Sin(Angle),
				Height);
			DrawDebugLine(GetWorld(), PreviousPoint, CurrentPoint, Color, false, 0.f, 0, Thickness);
			PreviousPoint = CurrentPoint;
		}
	}

	// 경도 호: 적도에서 상단/하단 극점까지 이어지는 세로 윤곽을 만든다.
	for (int32 LongitudeIndex = 0; LongitudeIndex < LongitudeSegments; ++LongitudeIndex)
	{
		const float Longitude = TWO_PI * static_cast<float>(LongitudeIndex) / static_cast<float>(LongitudeSegments);
		const FVector HorizontalDirection(FMath::Cos(Longitude), FMath::Sin(Longitude), 0.f);
		FVector PreviousPoint = Center + HorizontalDirection * Radius;

		for (int32 ArcIndex = 1; ArcIndex <= ArcSegments; ++ArcIndex)
		{
			const float Elevation = HALF_PI * static_cast<float>(ArcIndex) / static_cast<float>(ArcSegments);
			const FVector CurrentPoint = Center
				+ HorizontalDirection * (Radius * FMath::Cos(Elevation))
				+ FVector::UpVector * (VerticalSign * Radius * FMath::Sin(Elevation));
			DrawDebugLine(GetWorld(), PreviousPoint, CurrentPoint, Color, false, 0.f, 0, Thickness);
			PreviousPoint = CurrentPoint;
		}
	}
}

FString UYUFSNPCDebugComponent::BuildStateText(const FYUFSNPCObservation& Obs) const
{
	const UEnum* StateEnum = StaticEnum<EYUFSBehaviorState>();
	const UEnum* ActionEnum = StaticEnum<EYUFSAction>();

	const FString StateName = StateEnum
		? StateEnum->GetNameStringByValue(static_cast<int64>(Obs.CurrentState))
		: FString::FromInt(static_cast<int32>(Obs.CurrentState));
	const FString ActionName = ActionEnum
		? ActionEnum->GetNameStringByValue(static_cast<int64>(OwnerNPC->GetLastAction()))
		: FString::FromInt(static_cast<int32>(OwnerNPC->GetLastAction()));

	const UYUFSSmokeAwareNavigator* Navigator = OwnerNPC->GetNavigator();
	const FString DestinationText = Navigator
		? Navigator->GetCurrentDestination().ToCompactString()
		: FString(TEXT("None"));
	const FString PathStatus = (Navigator && Navigator->bIsPathfinding) ? TEXT("Repathing") : TEXT("Stable");

	return FString::Printf(
		TEXT("%s\nState: %s\nAction: %s\nRisk: %.2f | Env: %.2f\nDest: %s\nPath: %s"),
		*OwnerNPC->GetName(),
		*StateName,
		*ActionName,
		Obs.RiskPerception,
		Obs.RiskLevel,
		*DestinationText,
		*PathStatus);
}

FString UYUFSNPCDebugComponent::BuildObservationText(const FYUFSNPCObservation& Obs) const
{
	return FString::Printf(
		TEXT("Smoke Self/Front/Above: %.2f / %.2f / %.2f\nTemp: %.2f | Crowd: %d (%.2f)\nAlarm: %s | Staff: %s | HelpCue: %s\nExitDist: %.0f"),
		Obs.SmokeDensityAtSelf,
		Obs.SmokeInFrontNormalized,
		Obs.SmokeAboveNormalized,
		Obs.TemperatureAtSelf,
		Obs.NearbyNPCCount,
		Obs.NearbyEvacuatingRatio,
		Obs.bAlarmSounding ? TEXT("Y") : TEXT("N"),
		Obs.bReceivedStaffGuidance ? TEXT("Y") : TEXT("N"),
		Obs.bNearbyNPCNeedsHelp ? TEXT("Y") : TEXT("N"),
		Obs.DistToNearestExit);
}

void UYUFSNPCDebugComponent::DrawDebugOverlay(const FYUFSNPCObservation& Obs)
{
	if (!OwnerNPC.IsValid() || !GetWorld())
	{
		return;
	}

	AActor* OwnerActor = OwnerNPC.Get();
	const FVector ActorLocation = OwnerActor->GetActorLocation();
	const FColor RiskColor = GetRiskColor(FMath::Max(Obs.RiskPerception, Obs.RiskLevel));

	if (bShowState)
	{
		DrawDebugString(
			GetWorld(),
			FVector(0.f, 0.f, TextHeightOffset),
			BuildStateText(Obs),
			OwnerActor,
			RiskColor,
			0.f,
			true);
	}

	if (bShowObservation)
	{
		DrawDebugString(
			GetWorld(),
			FVector(0.f, 0.f, ObservationTextHeightOffset),
			BuildObservationText(Obs),
			OwnerActor,
			FColor::White,
			0.f,
			true);
	}

	if (bShowRiskLevel || OwnerNPC->bVisualizeRoutePreference)
	{
		const float SphereRadius = RiskRingRadius + (Obs.RiskPerception * 25.f);
		const FVector SphereCenter = ActorLocation + FVector(0.f, 0.f, 40.f);

		if (bShowRiskLevel)
		{
			// 하단 반구는 기존처럼 현재 위험도를 초록→노랑→빨강으로 표시한다.
			DrawDebugHemisphere(SphereCenter, SphereRadius, false, RiskColor, 2.f);
		}

		if (OwnerNPC->bVisualizeRoutePreference)
		{
			// 상단 반구는 70:20:10 경로 행동을 파랑/주황/초록으로 표시한다.
			const FColor RouteColor = OwnerNPC->GetActiveRouteDebugColor().ToFColor(true);
			DrawDebugHemisphere(SphereCenter, SphereRadius, true, RouteColor, 3.f);
		}
	}

	if (!bShowPath)
	{
		return;
	}

	const UYUFSSmokeAwareNavigator* Navigator = OwnerNPC->GetNavigator();
	if (!Navigator)
	{
		return;
	}

	const TArray<FVector>& PathPoints = Navigator->GetCurrentPathPoints();
	const int32 WaypointIndex = Navigator->GetCurrentWaypointIndex();
	const FVector ZOffset(0.f, 0.f, PathHeightOffset);

	if (PathPoints.Num() > 0)
	{
		FVector PreviousPoint = ActorLocation + ZOffset;
		for (int32 Index = WaypointIndex; Index < PathPoints.Num(); ++Index)
		{
			const FVector CurrentPoint = PathPoints[Index] + ZOffset;
			const FColor SegmentColor = (Index == WaypointIndex) ? FColor::Cyan : FColor(170, 170, 170);
			DrawDebugLine(GetWorld(), PreviousPoint, CurrentPoint, SegmentColor, false, 0.f, 0, 2.5f);
			DrawDebugSphere(GetWorld(), CurrentPoint, 12.f, 8, SegmentColor, false, 0.f, 0, 1.2f);
			PreviousPoint = CurrentPoint;
		}
	}

	const FVector Destination = Navigator->GetCurrentDestination();
	if (!Destination.IsZero())
	{
		DrawDebugDirectionalArrow(
			GetWorld(),
			ActorLocation + ZOffset,
			Destination + ZOffset,
			80.f,
			FColor::Orange,
			false,
			0.f,
			0,
			2.5f);
	}
}
