// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"

#include "Async/Async.h"
#include "EngineUtils.h"
#include "Fire/YUFSBinaryManager.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Level/YUFSLevelDataManager.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

UYUFSSmokeAwareNavigator::UYUFSSmokeAwareNavigator()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UYUFSSmokeAwareNavigator::BeginPlay()
{
	Super::BeginPlay();

	for (TActorIterator<AYUFSLevelDataManager> It(GetWorld()); It; ++It)
	{
		LevelDataMgr = *It;
		break;
	}

	for (TActorIterator<AYUFSBinaryManager> It(GetWorld()); It; ++It)
	{
		BinaryManager = *It;
		break;
	}
}

void UYUFSSmokeAwareNavigator::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	RerouteTimer += DeltaTime;
}

void UYUFSSmokeAwareNavigator::RequestPathAsync(FVector Destination, int32 Frame)
{
	if (bIsPathfinding)
	{
		return;
	}

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!NavSys)
	{
		return;
	}

	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (!OwnerCharacter)
	{
		return;
	}

	const FNavAgentProperties& AgentProps = OwnerCharacter->GetNavAgentPropertiesRef();
	ANavigationData* NavData = NavSys->GetNavDataForProps(AgentProps, OwnerCharacter->GetActorLocation());
	if (!NavData)
	{
		NavData = NavSys->GetDefaultNavDataInstance();
	}

	if (!NavData)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Red, TEXT("[Nav] Failed to find NavData!"));
		}
		return;
	}

	// 목적지를 NavMesh에 투영 — 실패하면 벽 안쪽으로 경로 탐색하므로 중단
	FNavLocation ProjectedDestination;
	if (!NavSys->ProjectPointToNavigation(Destination, ProjectedDestination, FVector(500.f, 500.f, 500.f), &AgentProps, nullptr))
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Red,
				FString::Printf(TEXT("[Nav] Destination NavMesh projection failed: %s"), *Destination.ToString()));
		}
		return;
	}
	Destination = ProjectedDestination.Location;

	// 시작점도 NavMesh에 투영 — NPC가 약간 NavMesh 밖에 있으면 경로가 깨짐
	FVector StartLocation = OwnerCharacter->GetActorLocation();
	FNavLocation ProjectedStart;
	if (NavSys->ProjectPointToNavigation(StartLocation, ProjectedStart, FVector(200.f, 200.f, 400.f), &AgentProps, nullptr))
	{
		StartLocation = ProjectedStart.Location;
	}

	FPathFindingQuery Query(
		OwnerCharacter,
		*NavData,
		StartLocation,
		Destination,
		UNavigationQueryFilter::GetQueryFilter(*NavData, OwnerCharacter, UYUFSSmokeNavigationQueryFilter::StaticClass()));

	bIsPathfinding = true;
	CurrentDestination = Destination;

	TWeakObjectPtr<UYUFSSmokeAwareNavigator> WeakThis(this);
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [WeakThis, NavSys, Query]()
	{
		if (!NavSys)
		{
			return;
		}

		FPathFindingResult PathResult = NavSys->FindPathSync(Query);
		TArray<FVector> ResultPathPoints;
		bool bSuccess = false;

		if (PathResult.IsSuccessful() && PathResult.Path.IsValid())
		{
			bSuccess = true;
			for (const FNavPathPoint& Point : PathResult.Path->GetPathPoints())
			{
				ResultPathPoints.Add(Point.Location);
			}
		}

		AsyncTask(ENamedThreads::GameThread, [WeakThis, bSuccess, ResultPathPoints]()
		{
			if (UYUFSSmokeAwareNavigator* NavComp = WeakThis.Get())
			{
				NavComp->bIsPathfinding = false;
				// 경로 탐색 완료 후 연기 패널티 초기화 — 다음 정상 탐색에 영향 없도록
				UYUFSSmokeNavigationQueryFilter::ResetSmokeCosts();

				if (bSuccess)
				{
					NavComp->CurrentPath = ResultPathPoints;
					NavComp->CurrentWaypointIndex = NavComp->CurrentPath.Num() > 1 ? 1 : 0;
				}
				else
				{
					if (GEngine)
					{
						GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Red, TEXT("[Nav] Custom Async Path Failed!"));
					}
					NavComp->ClearPath();
				}
			}
		});
	});
}

void UYUFSSmokeAwareNavigator::CheckAndReroute(int32 Frame)
{
	if (CurrentPath.IsEmpty() || bIsPathfinding)
	{
		return;
	}

	if (RerouteTimer >= RerouteCheckInterval)
	{
		RerouteTimer = 0.f;

		TArray<FVector> RemainingPath;
		for (int32 PathIndex = CurrentWaypointIndex; PathIndex < CurrentPath.Num(); ++PathIndex)
		{
			RemainingPath.Add(CurrentPath[PathIndex]);
		}

		const float DangerScore = LevelDataMgr ? LevelDataMgr->GetPathDangerScore(RemainingPath, Frame) : 0.f;
		if (DangerScore > SmokeBlockThreshold)
		{
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(
					-1,
					0.0f,
					FColor::Orange,
					FString::Printf(TEXT("[Nav] Rerouting due to Smoke! Danger Score: %f"), DangerScore));
			}
			// 재탐색 전 NavArea_Obstacle 비용을 높여 연기 구역을 우회하도록 유도
			// (레벨에 NavModifierVolume + NavArea_Obstacle 배치 시 실제 우회 경로 생성)
			UYUFSSmokeNavigationQueryFilter::UpdateSmokeCosts(BinaryManager, Frame);
			RequestPathAsync(CurrentDestination, Frame);
		}
	}
}

void UYUFSSmokeAwareNavigator::ClearPath()
{
	CurrentPath.Empty();
	CurrentWaypointIndex = 0;
	CurrentDestination = FVector::ZeroVector;
}

FVector UYUFSSmokeAwareNavigator::GetNextWaypoint() const
{
	if (CurrentPath.Num() > 0 && CurrentWaypointIndex < CurrentPath.Num())
	{
		return CurrentPath[CurrentWaypointIndex];
	}

	return GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
}

FVector UYUFSSmokeAwareNavigator::GetSteeringTarget(FVector ActorLocation, float LookAheadDistance) const
{
	if (CurrentPath.IsEmpty() || CurrentWaypointIndex >= CurrentPath.Num())
	{
		return ActorLocation;
	}

	// 현재 웨이포인트까지만 룩어헤드를 허용 — 그 이상 넘어가면 코너를 직선으로 관통하는
	// 방향벡터가 생겨 벽 돌진 현상이 발생하므로 현재 세그먼트 안으로 클램프
	FVector NextWaypoint = CurrentPath[CurrentWaypointIndex];
	NextWaypoint.Z = ActorLocation.Z;

	const float DistToNext = FVector::Dist2D(ActorLocation, NextWaypoint);

	// 웨이포인트가 룩어헤드 거리 안에 있으면 그냥 웨이포인트를 직접 목표로 사용
	if (DistToNext <= LookAheadDistance || DistToNext <= KINDA_SMALL_NUMBER)
	{
		return NextWaypoint;
	}

	// 웨이포인트까지 충분히 멀면 현재 세그먼트 안에서 룩어헤드 보간
	const FVector Dir = (NextWaypoint - ActorLocation).GetSafeNormal2D();
	FVector SteeringTarget = ActorLocation + Dir * LookAheadDistance;
	SteeringTarget.Z = ActorLocation.Z;
	return SteeringTarget;
}

void UYUFSSmokeAwareNavigator::UpdateWaypoint(FVector ActorLocation, float AcceptanceRadius)
{
	if (CurrentPath.Num() == 0 || CurrentWaypointIndex >= CurrentPath.Num())
	{
		return;
	}

	const float AcceptanceRadiusSq = FMath::Square(AcceptanceRadius);
	while (CurrentWaypointIndex < CurrentPath.Num())
	{
		FVector CurrentWaypoint = CurrentPath[CurrentWaypointIndex];
		CurrentWaypoint.Z = ActorLocation.Z;

		if (FVector::DistSquared2D(ActorLocation, CurrentWaypoint) > AcceptanceRadiusSq)
		{
			break;
		}

		++CurrentWaypointIndex;
	}
}

