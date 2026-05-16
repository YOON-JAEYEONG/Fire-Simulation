// Fill out your copyright notice in the Description page of Project Settings.


#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "EngineUtils.h"
#include "Level/YUFSLevelDataManager.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "Async/Async.h"

// Sets default values for this component's properties
UYUFSSmokeAwareNavigator::UYUFSSmokeAwareNavigator()
{
	PrimaryComponentTick.bCanEverTick = true;
}


// Called when the game starts
void UYUFSSmokeAwareNavigator::BeginPlay()
{
	Super::BeginPlay();

	// LevelDataManager 캐싱
	for (TActorIterator<AYUFSLevelDataManager> It(GetWorld()); It; ++It)
	{
		LevelDataMgr = *It;
		break;
	}
}


// Called every frame
void UYUFSSmokeAwareNavigator::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	RerouteTimer += DeltaTime;
}

void UYUFSSmokeAwareNavigator::RequestPathAsync(FVector Destination, int32 Frame)
{
	if (bIsPathfinding) return;

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!NavSys) return;

	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (!OwnerCharacter) return;

	// 해당 캐릭터의 에이전트 속성에 맞는 NavData(NavMesh)를 찾습니다.
	const FNavAgentProperties& AgentProps = OwnerCharacter->GetNavAgentPropertiesRef();
	ANavigationData* NavData = NavSys->GetNavDataForProps(AgentProps, OwnerCharacter->GetActorLocation());
	
	if (!NavData)
	{
		NavData = NavSys->GetDefaultNavDataInstance();
	}

	if (!NavData)
	{
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Red, TEXT("[Nav] Failed to find NavData!"));
		return;
	}

	// 기존에 완벽하게 작동하던 동기식 길찾기(FindPathSync)를 백그라운드 스레드로 분리합니다.
	// 언리얼의 복잡한 비동기 델리게이트 검증(Result:1)을 피하고 확실한 경로를 얻기 위함입니다.
	FPathFindingQuery Query(OwnerCharacter, *NavData, OwnerCharacter->GetActorLocation(), Destination, UNavigationQueryFilter::GetQueryFilter(*NavData, OwnerCharacter, nullptr));

	bIsPathfinding = true;
	CurrentDestination = Destination;

	TWeakObjectPtr<UYUFSSmokeAwareNavigator> WeakThis(this);
	
	// 백그라운드 스레드에서 길찾기 연산 수행 (게임 멈춤 방지)
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [WeakThis, NavSys, Query]()
	{
		if (!NavSys) return;
		
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

		// 연산이 끝나면 메인 게임 스레드로 결과를 넘겨줌
		AsyncTask(ENamedThreads::GameThread, [WeakThis, bSuccess, ResultPathPoints]()
		{
			if (UYUFSSmokeAwareNavigator* NavComp = WeakThis.Get())
			{
				NavComp->bIsPathfinding = false;
				if (bSuccess)
				{
					NavComp->CurrentPath = ResultPathPoints;
					// 0번은 현재 위치이므로 1번부터
					NavComp->CurrentWaypointIndex = NavComp->CurrentPath.Num() > 1 ? 1 : 0;
				}
				else
				{
					if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Red, TEXT("[Nav] Custom Async Path Failed!"));
					NavComp->ClearPath();
				}
			}
		});
	});
}

// 기존 OnPathFound 콜백 함수는 이제 사용하지 않으므로 비워둡니다.
void UYUFSSmokeAwareNavigator::OnPathFound(uint32 PathId, ENavigationQueryResult::Type Result, FNavPathSharedPtr NavPath)
{
}

void UYUFSSmokeAwareNavigator::CheckAndReroute(int32 Frame)
{
	if (CurrentPath.IsEmpty() || bIsPathfinding) return;

	RerouteTimer += GetWorld()->GetDeltaSeconds();
	if (RerouteTimer >= RerouteCheckInterval)
	{
		RerouteTimer = 0.f;

		// 현재 인덱스부터 끝까지의 남은 경로 추출
		TArray<FVector> RemainingPath;
		for (int32 i = CurrentWaypointIndex; i < CurrentPath.Num(); ++i)
		{
			RemainingPath.Add(CurrentPath[i]);
		}

		// 남은 경로의 위험도 재평가 (화재는 실시간으로 번지므로)
		float DangerScore = LevelDataMgr->GetPathDangerScore(RemainingPath, Frame);
		
		if (DangerScore > SmokeBlockThreshold)
		{
			// 기존 경로가 연기로 막혔다면 새로운 경로 요청
			if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 0.0f, FColor::Orange, FString::Printf(TEXT("[Nav] Rerouting due to Smoke! Danger Score: %f"), DangerScore));
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
	// 경로가 없으면 현재 위치 유지
	return GetOwner()->GetActorLocation();
}

void UYUFSSmokeAwareNavigator::UpdateWaypoint(FVector ActorLocation, float AcceptanceRadius)
{
	if (CurrentPath.Num() > 0 && CurrentWaypointIndex < CurrentPath.Num())
	{
		// 2D 평면 거리 기준으로 판단 (Z축은 계단 등에서 오차가 있을 수 있음)
		FVector CurrentWP = CurrentPath[CurrentWaypointIndex];
		CurrentWP.Z = ActorLocation.Z; 
		
		if (FVector::Distance(ActorLocation, CurrentWP) <= AcceptanceRadius)
		{
			CurrentWaypointIndex++;
		}
	}
}

float UYUFSSmokeAwareNavigator::EvaluatePathCost(const TArray<FVector>& Path, int32 Frame) const
{
	if (LevelDataMgr)
	{
		return LevelDataMgr->GetPathDangerScore(Path, Frame);
	}
	return 0.f;
}
