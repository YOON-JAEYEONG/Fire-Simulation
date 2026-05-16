// Fill out your copyright notice in the Description page of Project Settings.


#include "Level/YUFSLevelDataManager.h"

#include "YUFSExitPoint.h"
#include "YUFSShelterPoint.h"
#include "Fire/YUFSBinaryManager.h"
#include "Kismet/GameplayStatics.h"

// Sets default values
AYUFSLevelDataManager::AYUFSLevelDataManager()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void AYUFSLevelDataManager::BeginPlay()
{
	Super::BeginPlay();
	
	CollectLevelActors(); // 에디터 배치 Actor 자동 수집
	BinaryManager = Cast<AYUFSBinaryManager>(
		UGameplayStatics::GetActorOfClass(GetWorld(), AYUFSBinaryManager::StaticClass()));
}

// Called every frame
void AYUFSLevelDataManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void AYUFSLevelDataManager::CollectLevelActors()
{
	TArray<AActor*> FoundExits;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AYUFSExitPoint::StaticClass(), FoundExits);
	for (AActor* Actor : FoundExits)
	{
		CachedExits.Add(Cast<AYUFSExitPoint>(Actor));
	}

	TArray<AActor*> FoundShelters;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AYUFSShelterPoint::StaticClass(), FoundShelters);
	for (AActor* Actor : FoundShelters)
	{
		CachedShelters.Add(Cast<AYUFSShelterPoint>(Actor));
	}
	
}

FVector AYUFSLevelDataManager::GetNearestSafeExit(FVector From, bool bSmokeFreeOnly, int32 Frame) const
{
	FVector NearestLocation = From;
	float MinDistSq = MAX_flt;
	
	for (AYUFSExitPoint* Exit : CachedExits)
	{
		if (!IsValid(Exit)) continue;
		
		FVector ExitLoc = Exit->GetActorLocation();
		
		if (bSmokeFreeOnly && IsLocationDangerous(ExitLoc, Frame))
		{
			continue;
		}
		
		float DistSq = FVector::DistSquared(From, ExitLoc);
		if (DistSq < MinDistSq)
		{
			MinDistSq = DistSq;
			NearestLocation = ExitLoc;
		}
	}
	
	return NearestLocation;
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

FVector AYUFSLevelDataManager::GetNearestAvailableShelter(FVector From) const
{
	FVector NearestLocation = From;
	float MinDistSq = MAX_flt;
	
	for (AYUFSShelterPoint* Shelter : CachedShelters)
	{
		if (!IsValid(Shelter)) continue;
		
		FVector ShelterLoc = Shelter->GetActorLocation();
		float DistSq = FVector::DistSquared(From, ShelterLoc);
		if (DistSq < MinDistSq)
		{
			MinDistSq = DistSq;
			NearestLocation = ShelterLoc;
		}
	}
	
	return NearestLocation;
}

bool AYUFSLevelDataManager::IsLocationDangerous(FVector Location, int32 Frame) const
{
	if (!BinaryManager) return false;
	
	uint8 OutDensity = 0;
	if (BinaryManager->GetSmokeDensityAtLocation(Location, Frame, OutDensity))
	{
		float NormalizedDensity = OutDensity / 255.0f;
		return NormalizedDensity >= DangerSmokeDensityThreshold;
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
