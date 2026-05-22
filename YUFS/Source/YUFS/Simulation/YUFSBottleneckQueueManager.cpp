// Fill out your copyright notice in the Description page of Project Settings.

#include "Simulation/YUFSBottleneckQueueManager.h"

#include "EngineUtils.h"
#include "Level/YUFSBottleneckPoint.h"
#include "NavigationSystem.h"

AYUFSBottleneckQueueManager::AYUFSBottleneckQueueManager()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AYUFSBottleneckQueueManager::BeginPlay()
{
	Super::BeginPlay();
	CollectBottlenecks();
}

void AYUFSBottleneckQueueManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	CompactQueues();
}

FVector AYUFSBottleneckQueueManager::ResolveMovementTarget(
	AActor* Requester,
	const TArray<FVector>& CurrentPath,
	const FVector& FinalDestination)
{
	if (!IsValid(Requester))
	{
		return FinalDestination;
	}

	CompactQueues();

	int32 ExistingQueueIndex = INDEX_NONE;
	if (AYUFSBottleneckPoint* AssignedBottleneck = FindAssignedBottleneck(Requester, ExistingQueueIndex))
	{
		if (HasPassedBottleneck(Requester, AssignedBottleneck))
		{
			ReleaseRequester(Requester);
			return FinalDestination;
		}

		const int32 LaneCount = FMath::Max(1, AssignedBottleneck->MaxConcurrentLanes);
		if (ExistingQueueIndex >= 0 && ExistingQueueIndex < LaneCount)
		{
			return BuildPassThroughTarget(AssignedBottleneck);
		}

		return BuildQueueSlotLocation(AssignedBottleneck, ExistingQueueIndex);
	}

	AYUFSBottleneckPoint* NextBottleneck = FindNextBottleneck(Requester, CurrentPath, FinalDestination);
	if (!IsValid(NextBottleneck))
	{
		return FinalDestination;
	}

	FYUFSBottleneckQueueState& QueueState = GetOrCreateQueueState(NextBottleneck);
	const int32 QueueIndex = ReserveQueueIndex(Requester, QueueState);
	const int32 LaneCount = FMath::Max(1, NextBottleneck->MaxConcurrentLanes);

	if (QueueIndex < LaneCount)
	{
		return BuildPassThroughTarget(NextBottleneck);
	}

	return BuildQueueSlotLocation(NextBottleneck, QueueIndex);
}

void AYUFSBottleneckQueueManager::ReleaseRequester(AActor* Requester)
{
	if (!IsValid(Requester))
	{
		return;
	}

	for (FYUFSBottleneckQueueState& QueueState : QueueStates)
	{
		for (int32 Index = QueueState.Occupants.Num() - 1; Index >= 0; --Index)
		{
			if (QueueState.Occupants[Index].Get() == Requester)
			{
				QueueState.Occupants.RemoveAt(Index);
			}
		}
	}
}

void AYUFSBottleneckQueueManager::CollectBottlenecks()
{
	CachedBottlenecks.Empty();
	for (TActorIterator<AYUFSBottleneckPoint> It(GetWorld()); It; ++It)
	{
		CachedBottlenecks.Add(*It);
	}
}

void AYUFSBottleneckQueueManager::CompactQueues()
{
	for (FYUFSBottleneckQueueState& QueueState : QueueStates)
	{
		CompactQueue(QueueState);
	}
}

void AYUFSBottleneckQueueManager::CompactQueue(FYUFSBottleneckQueueState& QueueState)
{
	for (int32 Index = QueueState.Occupants.Num() - 1; Index >= 0; --Index)
	{
		AActor* Occupant = QueueState.Occupants[Index].Get();
		if (!IsValid(Occupant) || !IsValid(QueueState.Bottleneck) || HasPassedBottleneck(Occupant, QueueState.Bottleneck))
		{
			QueueState.Occupants.RemoveAt(Index);
		}
	}
}

AYUFSBottleneckPoint* AYUFSBottleneckQueueManager::FindAssignedBottleneck(AActor* Requester, int32& OutQueueIndex)
{
	OutQueueIndex = INDEX_NONE;

	for (FYUFSBottleneckQueueState& QueueState : QueueStates)
	{
		for (int32 Index = 0; Index < QueueState.Occupants.Num(); ++Index)
		{
			if (QueueState.Occupants[Index].Get() == Requester)
			{
				OutQueueIndex = Index;
				return QueueState.Bottleneck;
			}
		}
	}

	return nullptr;
}

AYUFSBottleneckPoint* AYUFSBottleneckQueueManager::FindNextBottleneck(
	AActor* Requester,
	const TArray<FVector>& CurrentPath,
	const FVector& FinalDestination) const
{
	// 경로가 없으면 직선으로 병목 감지 → 벽 통과 오탐 발생
	// NavMesh 경로가 확보된 뒤에만 병목 감지
	if (CurrentPath.Num() == 0)
	{
		return nullptr;
	}

	TArray<FVector> PathPoints = CurrentPath;
	if (PathPoints.Num() == 1)
	{
		PathPoints.Insert(Requester->GetActorLocation(), 0);
		PathPoints.Add(FinalDestination);
	}

	AYUFSBottleneckPoint* BestBottleneck = nullptr;
	float BestDistance = MAX_flt;

	for (AYUFSBottleneckPoint* Bottleneck : CachedBottlenecks)
	{
		if (!IsValid(Bottleneck))
		{
			continue;
		}

		if (HasPassedBottleneck(Requester, Bottleneck))
		{
			continue;
		}

		const float DistanceToPath = GetMinDistanceToPath2D(Bottleneck->GetActorLocation(), PathPoints);
		if (DistanceToPath > Bottleneck->DetectionRadiusCm)
		{
			continue;
		}

		if (DistanceToPath < BestDistance)
		{
			BestDistance = DistanceToPath;
			BestBottleneck = Bottleneck;
		}
	}

	return BestBottleneck;
}

FYUFSBottleneckQueueState& AYUFSBottleneckQueueManager::GetOrCreateQueueState(AYUFSBottleneckPoint* Bottleneck)
{
	for (FYUFSBottleneckQueueState& QueueState : QueueStates)
	{
		if (QueueState.Bottleneck == Bottleneck)
		{
			return QueueState;
		}
	}

	FYUFSBottleneckQueueState& NewState = QueueStates.AddDefaulted_GetRef();
	NewState.Bottleneck = Bottleneck;
	return NewState;
}

int32 AYUFSBottleneckQueueManager::ReserveQueueIndex(AActor* Requester, FYUFSBottleneckQueueState& QueueState)
{
	for (int32 Index = 0; Index < QueueState.Occupants.Num(); ++Index)
	{
		if (QueueState.Occupants[Index].Get() == Requester)
		{
			return Index;
		}
	}

	return QueueState.Occupants.Add(Requester);
}

bool AYUFSBottleneckQueueManager::HasPassedBottleneck(const AActor* Requester, const AYUFSBottleneckPoint* Bottleneck) const
{
	if (!IsValid(Requester) || !IsValid(Bottleneck))
	{
		return false;
	}

	FVector Relative = Requester->GetActorLocation() - Bottleneck->GetActorLocation();
	Relative.Z = 0.f;
	const FVector Forward = Bottleneck->GetActorForwardVector().GetSafeNormal2D();

	return FVector::DotProduct(Relative, Forward) >= Bottleneck->ReleaseForwardDistanceCm;
}

FVector AYUFSBottleneckQueueManager::BuildQueueSlotLocation(const AYUFSBottleneckPoint* Bottleneck, int32 QueueIndex) const
{
	if (!IsValid(Bottleneck))
	{
		return FVector::ZeroVector;
	}

	const int32 LaneCount = FMath::Clamp(
		FMath::FloorToInt(Bottleneck->PassageWidth / FMath::Max(1.f, Bottleneck->LaneSpacingCm)),
		1,
		FMath::Max(1, Bottleneck->MaxConcurrentLanes));
	const int32 RowIndex = QueueIndex / LaneCount;
	const int32 LaneIndex = QueueIndex % LaneCount;
	const float LaneCenter = (static_cast<float>(LaneCount) - 1.f) * 0.5f;
	const float LateralOffset = (static_cast<float>(LaneIndex) - LaneCenter) * Bottleneck->LaneSpacingCm;

	const FVector Forward = Bottleneck->GetActorForwardVector().GetSafeNormal2D();
	const FVector QueueDirection = -Forward;
	const FVector Right(-QueueDirection.Y, QueueDirection.X, 0.f);

	const float DepthOffset = Bottleneck->QueueFrontOffsetCm + (static_cast<float>(RowIndex) * Bottleneck->QueueRowSpacingCm);
	const FVector SlotLocation = Bottleneck->GetActorLocation() + (QueueDirection * DepthOffset) + (Right * LateralOffset);

	return ProjectToNavigation(SlotLocation);
}

FVector AYUFSBottleneckQueueManager::BuildPassThroughTarget(const AYUFSBottleneckPoint* Bottleneck) const
{
	if (!IsValid(Bottleneck))
	{
		return FVector::ZeroVector;
	}

	const FVector Forward = Bottleneck->GetActorForwardVector().GetSafeNormal2D();
	const FVector Target = Bottleneck->GetActorLocation() + (Forward * Bottleneck->PassThroughOffsetCm);
	return ProjectToNavigation(Target);
}

FVector AYUFSBottleneckQueueManager::ProjectToNavigation(const FVector& Location) const
{
	if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld()))
	{
		FNavLocation ProjectedLocation;
		// 범위를 넉넉하게 설정 — 범위가 너무 작으면 병목 근처 슬롯이 NavMesh 밖으로 떨어져 ZeroVector 반환
		if (NavSys->ProjectPointToNavigation(Location, ProjectedLocation, FVector(300.f, 300.f, 400.f)))
		{
			return ProjectedLocation.Location;
		}
	}

	// 투영 실패 → ZeroVector 반환, 호출부에서 원래 목적지로 폴백
	return FVector::ZeroVector;
}

float AYUFSBottleneckQueueManager::GetMinDistanceToPath2D(const FVector& Location, const TArray<FVector>& PathPoints) const
{
	if (PathPoints.Num() == 0)
	{
		return MAX_flt;
	}

	if (PathPoints.Num() == 1)
	{
		return FVector::Dist2D(Location, PathPoints[0]);
	}

	float MinDistanceSq = MAX_flt;
	const FVector FlatLocation(Location.X, Location.Y, 0.f);

	for (int32 Index = 0; Index < PathPoints.Num() - 1; ++Index)
	{
		const FVector SegmentStart(PathPoints[Index].X, PathPoints[Index].Y, 0.f);
		const FVector SegmentEnd(PathPoints[Index + 1].X, PathPoints[Index + 1].Y, 0.f);
		const FVector ClosestPoint = FMath::ClosestPointOnSegment(FlatLocation, SegmentStart, SegmentEnd);
		MinDistanceSq = FMath::Min(MinDistanceSq, FVector::DistSquared(FlatLocation, ClosestPoint));
	}

	return FMath::Sqrt(MinDistanceSq);
}
