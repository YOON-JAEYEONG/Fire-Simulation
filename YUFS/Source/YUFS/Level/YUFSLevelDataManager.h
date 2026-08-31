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

public:
	FVector GetNearestSafeExit(FVector From, bool bSmokeFreeOnly, int32 Frame) const;
	bool TryGetNearestSafeExit(FVector From, int32 Frame, FVector& OutExitLocation) const;
	FVector GetFamiliarExit(FVector NPCSpawnLocation) const;
	bool IsLocationDangerous(FVector Location, int32 Frame) const;
	float GetPathDangerScore(const TArray<FVector>& Path, int32 Frame) const;
};
