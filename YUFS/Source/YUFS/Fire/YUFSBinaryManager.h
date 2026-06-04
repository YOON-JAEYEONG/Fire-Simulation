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

protected:	
	void PlayDebugAnimation();

public:

	void SetHeterogeneousVolume(AYUFSHeterogeneousVolume* InVolume);

	bool GetSmokeDensityAtLocation(FVector WorldLocation, int32 FrameIndex, uint8& OutDensity);
	bool GetTemperatureAtLocation(FVector WorldLocation, int32 FrameIndex, uint8& OutTemperature);
	
	int32 GetCurrentFrame() const { return CurrentDebugFrame; }
	AYUFSHeterogeneousVolume* GetHeterogeneousVolume() const { return HeterogeneousVolume; }

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

	UPROPERTY(EditAnywhere, Category="Fire")
	FString BinaryFilePath = TEXT("Fires/FirePrototype/BinaryData/smoke_data.bin");
	
	// true로 설정하면 매 100ms마다 복셀 디버그 박스를 월드에 그림 (에디터 전용)
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

	// 동적 스트리밍 관련 변수
	const int32 MaxBufferSize = 400; // 앞뒤 200프레임 (여유롭게 400 고정)
	bool bIsLoadingChunk = false;
	int32 LoadGeneration = 0;
	int32 LastCurrentFrame = -1;
};
