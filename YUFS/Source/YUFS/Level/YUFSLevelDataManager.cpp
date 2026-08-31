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

bool AYUFSLevelDataManager::TryGetNearestSafeExit(FVector From, int32 Frame, FVector& OutExit) const
{
	RefreshExitDangerCache(Frame);
	OutExit = FVector::ZeroVector;
	float MinSafeDist = MAX_flt;

	for (int32 Index = 0; Index < CachedExits.Num(); ++Index)
	{
		AYUFSExitPoint* Exit = CachedExits[Index];
		if (!IsValid(Exit)) continue;

		const int8 DangerState = CachedExitDangerStates.IsValidIndex(Index)
			? CachedExitDangerStates[Index]
			: -1;
		const bool bDangerous = DangerState == 1 || (DangerState < 0 && bTreatUnknownExitAsDangerous);
		if (bDangerous) continue;

		const float DistSq = FVector::DistSquared(From, Exit->GetActorLocation());
		if (DistSq < MinSafeDist)
		{
			MinSafeDist = DistSq;
			OutExit = Exit->GetActorLocation();
		}
	}

	return MinSafeDist < MAX_flt;
}

FVector AYUFSLevelDataManager::GetNearestSafeExit(FVector From, bool bSmokeFreeOnly, int32 Frame) const
{
	RefreshExitDangerCache(Frame);

	if (bSmokeFreeOnly)
	{
		FVector SafeExit = FVector::ZeroVector;
		// 모두 위험하거나 화재 데이터가 확인되지 않으면 현재 위치를 반환한다.
		// 호출자는 TryGetNearestSafeExit로 SHELTER 전환 여부를 명시적으로 판단한다.
		return TryGetNearestSafeExit(From, Frame, SafeExit) ? SafeExit : From;
	}

	FVector NearestAny = FVector::ZeroVector;
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
	int32 SampleCount = 0;
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
				++SampleCount;
			}
		}
	}
	
	// 경로 길이 자체가 위험도로 중복 계산되지 않도록 유효 샘플 평균을 반환한다.
	return SampleCount > 0 ? TotalScore / static_cast<float>(SampleCount) : 0.f;
}
