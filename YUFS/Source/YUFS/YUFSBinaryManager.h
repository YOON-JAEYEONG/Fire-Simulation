// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HAL/ThreadSafeCounter.h"
#include "YUFSBinaryManager.generated.h"


class AYUFSHeterogeneousVolume;

USTRUCT()
struct FFrameData
{
	GENERATED_BODY()
	
	TArray<uint8> DensityGrid;
	TArray<uint8> TemperatureGrid;
};

UCLASS()
class YUFS_API AYUFSBinaryManager : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AYUFSBinaryManager();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

protected:	
	void PlayDebugAnimation();

public:
	bool GetSmokeDensityAtLocation(FVector WorldLocation, int32 FrameIndex, uint8& OutDensity);
	bool GetTemperatureAtLocation(FVector WorldLocation, int32 FrameIndex, uint8& OutTemperature);
	void LoadChunkAsync(int32 StartFrame, int32 EndFrame);
	
	AYUFSHeterogeneousVolume* GetHeterogeneousVolume() const { return HeterogeneousVolume; }
	
protected:
	
	UPROPERTY()
	TArray<FFrameData> FramesData;

	UPROPERTY()
	AYUFSHeterogeneousVolume* HeterogeneousVolume;
	
private:
	FThreadSafeCounter LoadedFramesCount;
	
	const int32 TotalFrames = 8000;
	int32 ChunkSize = 50; 
	int32 CurrentDebugFrame = 0;
	FTimerHandle DebugTimerHandle;
	float VoxelSize = 20.0f;
	int32 DebugStep = 2;
	uint8 DensityThreshold = 10;
	uint8 TemperatureThreshold = 10;
	FColor DensityColor = FColor::Black;
	FColor TemperatureColor = FColor::Red;
	bool bPauseAnimation = false;
	
	const int32 DimX = 153;
	const int32 DimY = 115;
	const int32 DimZ = 17;
};
