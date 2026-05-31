// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
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
	AYUFSBinaryManager();

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

	// FireActive 진입 시 SimulationController가 호출
	// HeterogeneousVolume에서 경로·원점을 가져와 버퍼를 초기화하고 스트리밍 시작
	void InitializeForScenario();

	bool GetSmokeDensityAtLocation(FVector WorldLocation, int32 FrameIndex, uint8& OutDensity);
	bool GetTemperatureAtLocation(FVector WorldLocation, int32 FrameIndex, uint8& OutTemperature);

	int32 GetCurrentFrame() const { return CurrentDebugFrame; }

protected:
	void PlayDebugAnimation();

private:
	void LoadDynamicChunkAsync(int32 StartFrame, int32 EndFrame, int32 Generation);

protected:
	UPROPERTY()
	TArray<FFrameData> FramesBuffer;

	UPROPERTY()
	TArray<int32> LoadedFrameIndices;

	UPROPERTY()
	AYUFSHeterogeneousVolume* HeterogeneousVolume;

private:
	UPROPERTY(VisibleAnywhere, Category="Fire")
	int32 TotalFrames = 0;

	UPROPERTY(EditAnywhere, Category="Fire")
	int32 ChunkSize = 50;

	int32 CurrentDebugFrame = 0;
	FTimerHandle DebugTimerHandle;

	UPROPERTY(EditAnywhere, Category="Fire")
	float VoxelSize = 40.0f;

	// InitializeForScenario() 시 HeterogeneousVolume에서 가져옴
	FString ActiveBinaryPath;
	FVector WorldOrigin = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category="Fire|Debug")
	bool bDrawVoxelDebug = false;

	int32 DebugStep = 2;
	uint8 DensityThreshold = 10;
	uint8 TemperatureThreshold = 10;
	FColor DensityColor = FColor::Black;
	FColor TemperatureColor = FColor::Red;

	UPROPERTY(VisibleAnywhere, Category="Fire")
	int32 DimX = 0;

	UPROPERTY(VisibleAnywhere, Category="Fire")
	int32 DimY = 0;

	UPROPERTY(VisibleAnywhere, Category="Fire")
	int32 DimZ = 0;

	const int32 MaxBufferSize = 400;
	bool bIsLoadingChunk = false;
	int32 LoadGeneration = 0;
	int32 LastCurrentFrame = -1;
};
