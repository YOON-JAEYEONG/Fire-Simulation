// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "YUFSLevelDataManager.generated.h"

class AYUFSBinaryManager;
class AYUFSExitPoint;

UCLASS()
class YUFS_API AYUFSLevelDataManager : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AYUFSLevelDataManager();

protected:
	virtual void BeginPlay() override;

private:
	void CollectLevelActors();

	UPROPERTY()
	TArray<AYUFSExitPoint*> CachedExits;
	UPROPERTY()
	AYUFSBinaryManager* BinaryManager;

	UPROPERTY(EditAnywhere)
	float DangerSmokeDensityThreshold = 0.35f;

	// 출구 위험도는 화재 프레임 안에서는 모든 NPC가 공유한다.
	// -1은 해당 프레임의 Grid 데이터가 아직 준비되지 않았음을 의미한다.
	mutable int32 CachedDangerFrame = INDEX_NONE;
	mutable TArray<int8> CachedExitDangerStates;
	void RefreshExitDangerCache(int32 Frame) const;

public:
	FVector GetNearestSafeExit(FVector From, bool bSmokeFreeOnly, int32 Frame) const;
	FVector GetFamiliarExit(FVector NPCSpawnLocation) const;
	bool IsLocationDangerous(FVector Location, int32 Frame) const;
	float GetPathDangerScore(const TArray<FVector>& Path, int32 Frame) const;
};
