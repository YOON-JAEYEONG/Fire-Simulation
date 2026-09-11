// Fill out your copyright notice in the Description page of Project Settings.


#include "Level/YUFSLevelDataManager.h"

#include "YUFSExitPoint.h"
#include "Fire/YUFSBinaryManager.h"
#include "Kismet/GameplayStatics.h"

// Sets default values
AYUFSLevelDataManager::AYUFSLevelDataManager()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AYUFSLevelDataManager::BeginPlay()
{
	Super::BeginPlay();
	CollectLevelActors();
	BinaryManager = Cast<AYUFSBinaryManager>(
		UGameplayStatics::GetActorOfClass(GetWorld(), AYUFSBinaryManager::StaticClass()));
}

void AYUFSLevelDataManager::CollectLevelActors()
{
	TArray<AActor*> FoundExits;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AYUFSExitPoint::StaticClass(), FoundExits);
	for (AActor* Actor : FoundExits)
	{
		CachedExits.Add(Cast<AYUFSExitPoint>(Actor));
	}
	CachedDangerFrame = INDEX_NONE;
	CachedExitDangerStates.Reset();

}

void AYUFSLevelDataManager::RefreshExitDangerCache(int32 Frame) const
{
	if (CachedDangerFrame == Frame && CachedExitDangerStates.Num() == CachedExits.Num())
	{
		return;
	}

	CachedDangerFrame = Frame;
	CachedExitDangerStates.Init(-1, CachedExits.Num());
	if (!BinaryManager)
	{
		return;
	}

	for (int32 Index = 0; Index < CachedExits.Num(); ++Index)
	{
		const AYUFSExitPoint* Exit = CachedExits[Index];
		if (!IsValid(Exit))
		{
			continue;
		}

		uint8 Density = 0;
		if (BinaryManager->GetSmokeDensityAtLocation(Exit->GetActorLocation(), Frame, Density))
		{
			CachedExitDangerStates[Index] =
				(Density / 255.f >= DangerSmokeDensityThreshold) ? 1 : 0;
		}
	}
}

FVector AYUFSLevelDataManager::GetNearestSafeExit(FVector From, bool bSmokeFreeOnly, int32 Frame) const
{
	RefreshExitDangerCache(Frame);

	// 한 번의 순회로 두 후보를 동시에 추적:
	// - NearestSmokeFree: 연기 없는 출구 중 가장 가까운 것
	// - NearestAny: 연기 유무 무관하게 가장 가까운 것 (bSmokeFreeOnly 폴백용)
	FVector NearestSmokeFree = FVector::ZeroVector;
	FVector NearestAny = FVector::ZeroVector;
	float MinSmokeFreeDist = MAX_flt;
	float MinAnyDist = MAX_flt;

	for (int32 Index = 0; Index < CachedExits.Num(); ++Index)
	{
		AYUFSExitPoint* Exit = CachedExits[Index];
		if (!IsValid(Exit)) continue;

		const FVector ExitLoc = Exit->GetActorLocation();
		const float DistSq = FVector::DistSquared(From, ExitLoc);

		if (DistSq < MinAnyDist)
		{
			MinAnyDist = DistSq;
			NearestAny = ExitLoc;
		}

		const bool bDangerous = CachedExitDangerStates.IsValidIndex(Index)
			? CachedExitDangerStates[Index] == 1
			: IsLocationDangerous(ExitLoc, Frame);
		if (!bDangerous && DistSq < MinSmokeFreeDist)
		{
			MinSmokeFreeDist = DistSq;
			NearestSmokeFree = ExitLoc;
		}
	}

	if (bSmokeFreeOnly)
	{
		// 연기 없는 출구 우선 — 모두 막혔으면 연기 있어도 가장 가까운 출구로 폴백
		// (폴백 없이 From을 반환하면 NPC가 자기 위치를 목적지로 설정해 무한 대기)
		if (MinSmokeFreeDist < MAX_flt) return NearestSmokeFree;
		if (MinAnyDist < MAX_flt)       return NearestAny;
		return From;
	}

	return MinAnyDist < MAX_flt ? NearestAny : From;
}

FVector AYUFSLevelDataManager::GetFamiliarExit(FVector NPCSpawnLocation) const
{
	FVector NearestLocation = NPCSpawnLocation;
	float MinDistSq = MAX_flt;
	
	for (AYUFSExitPoint* Exit : CachedExits)
	{
		if (!IsValid(Exit)) continue;
		
		FVector ExitLoc = Exit->GetActorLocation();
		float DistSq = FVector::DistSquared(NPCSpawnLocation, ExitLoc);
		if (DistSq < MinDistSq)
		{
			MinDistSq = DistSq;
			NearestLocation = ExitLoc;
		}
	}
	
	return NearestLocation;
}

bool AYUFSLevelDataManager::IsLocationDangerous(FVector Location, int32 Frame) const
{
	if (!BinaryManager) return false;
	RefreshExitDangerCache(Frame);

	// 출구 위치라면 같은 화재 프레임에 이미 조회한 위험도를 재사용한다.
	for (int32 Index = 0; Index < CachedExits.Num(); ++Index)
	{
		const AYUFSExitPoint* Exit = CachedExits[Index];
		if (IsValid(Exit) && Exit->GetActorLocation().Equals(Location, 1.f)
			&& CachedExitDangerStates.IsValidIndex(Index)
			&& CachedExitDangerStates[Index] >= 0)
		{
			return CachedExitDangerStates[Index] == 1;
		}
	}
	
	uint8 OutDensity = 0;
	if (BinaryManager->GetSmokeDensityAtLocation(Location, Frame, OutDensity))
	{
		const bool bDangerous = OutDensity / 255.f >= DangerSmokeDensityThreshold;
		for (int32 Index = 0; Index < CachedExits.Num(); ++Index)
		{
			const AYUFSExitPoint* Exit = CachedExits[Index];
			if (IsValid(Exit) && Exit->GetActorLocation().Equals(Location, 1.f)
				&& CachedExitDangerStates.IsValidIndex(Index))
			{
				CachedExitDangerStates[Index] = bDangerous ? 1 : 0;
				break;
			}
		}
		return bDangerous;
	}
	
	return false;
}

float AYUFSLevelDataManager::GetPathDangerScore(const TArray<FVector>& Path, int32 Frame) const
{
	if (!BinaryManager || Path.IsEmpty()) return 0.0f;
	
	float TotalScore = 0.0f;
	float StepSize = 50.0f; // 50cm 간격으로 선분 위를 촘촘하게 검사

	for (int32 i = 0; i < Path.Num() - 1; ++i)
	{
		FVector Start = Path[i];
		FVector End = Path[i + 1];
		float SegmentLength = FVector::Distance(Start, End);
		
		int32 Steps = FMath::Max(1, FMath::CeilToInt(SegmentLength / StepSize));
		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			float Alpha = (float)Step / Steps;
			FVector CheckPoint = FMath::Lerp(Start, End, Alpha);
			
			uint8 OutDensity = 0;
			if (BinaryManager->GetSmokeDensityAtLocation(CheckPoint, Frame, OutDensity))
			{
				TotalScore += (OutDensity / 255.0f);
			}
		}
	}
	
	return TotalScore;
}
