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
		TEXT("Smoke Self/Front/Above: %.2f / %.2f / %.2f\nTemp: %.2f | Crowd: %d (%.2f)\nAlarm: %s | Staff: %s | HelpCue: %s\nExitDist: %.0f | ShelterDist: %.0f"),
		Obs.SmokeDensityAtSelf,
		Obs.SmokeInFrontNormalized,
		Obs.SmokeAboveNormalized,
		Obs.TemperatureAtSelf,
		Obs.NearbyNPCCount,
		Obs.NearbyEvacuatingRatio,
		Obs.bAlarmSounding ? TEXT("Y") : TEXT("N"),
		Obs.bReceivedStaffGuidance ? TEXT("Y") : TEXT("N"),
		Obs.bNearbyNPCNeedsHelp ? TEXT("Y") : TEXT("N"),
		Obs.DistToNearestExit,
		Obs.DistToNearestShelter);
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
			ActorLocation + FVector(0.f, 0.f, TextHeightOffset),
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
			ActorLocation + FVector(0.f, 0.f, ObservationTextHeightOffset),
			BuildObservationText(Obs),
			OwnerActor,
			FColor::White,
			0.f,
			true);
	}

	if (bShowRiskLevel)
	{
		const float SphereRadius = RiskRingRadius + (Obs.RiskPerception * 25.f);
		DrawDebugSphere(
			GetWorld(),
			ActorLocation + FVector(0.f, 0.f, 40.f),
			SphereRadius,
			12,
			RiskColor,
			false,
			0.f,
			0,
			2.f);
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
