// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Fire/YUFSHazardField.h"
#include "YUFSBinaryManager.generated.h"

class AYUFSHeterogeneousVolume;

UCLASS()
class YUFS_API AYUFSBinaryManager : public AActor
{
	GENERATED_BODY()
	
public:	
	AYUFSBinaryManager();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:	
	virtual void Tick(float DeltaTime) override;

protected:	
	void PlayDebugAnimation();

public:

	void SetHeterogeneousVolume(AYUFSHeterogeneousVolume* InVolume);

	bool GetSmokeDensityAtLocation(FVector WorldLocation, int32 FrameIndex, uint8& OutDensity);
	bool GetTemperatureAtLocation(FVector WorldLocation, int32 FrameIndex, uint8& OutTemperature);
	FYUFSHazardSnapshot GetHazardSnapshot(int32 FrameIndex) const;
	UFUNCTION(BlueprintPure, Category="Fire|Diagnostics")
	EYUFSHazardDataStatus GetDataStatus() const { return GetHazardSnapshot(CurrentDebugFrame).Status; }
	UFUNCTION(BlueprintPure, Category="Fire|Diagnostics")
	FString GetHazardDiagnostics() const;
	
	int32 GetCurrentFrame() const { return CurrentDebugFrame; }
	AYUFSHeterogeneousVolume* GetHeterogeneousVolume() const { return HeterogeneousVolume; }

private:
	void LoadDynamicChunkAsync(int32 StartFrame, int32 EndFrame, int32 Generation);

protected:
	TArray<TSharedPtr<const FYUFSHazardGrid, ESPMode::ThreadSafe>> FramesBuffer;
	TArray<int32> LoadedFrameIndices;

	UPROPERTY()
	AYUFSHeterogeneousVolume* HeterogeneousVolume;
	
private:
	UPROPERTY(VisibleAnywhere, Category="Fire")
	int32 TotalFrames = 0;
	
	UPROPERTY(EditAnywhere, Category="Fire")
	int32 ChunkSize = 24; 
	
	int32 CurrentDebugFrame = 0;
	FTimerHandle DebugTimerHandle;
	
	UPROPERTY(EditAnywhere, Category="Fire")
	float VoxelSize = 40.0f;

	// Binary export metadata. Keep zero/one defaults compatible with the IT branch.
	// These values must be checked against the export script; the file header does not contain them.
	UPROPERTY(EditAnywhere, Category="Fire|Data Alignment")
	FVector GridOriginLocal = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, Category="Fire|Data Alignment", meta=(ClampMin="0.001"))
	float BinaryFramesPerVisualFrame = 1.f;
	UPROPERTY(EditAnywhere, Category="Fire|Data Alignment")
	int32 BinaryFrameOffset = 0;
	UPROPERTY(EditAnywhere, Category="Fire|Data Alignment")
	bool bDatasetAlignmentConfirmed = false;

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
	const int32 MaxBufferSize = 192;
	bool bIsLoadingChunk = false;
	int32 LoadGeneration = 0;
	int32 LastCurrentFrame = -1;
	bool bHeaderValid = false;
	friend struct FYUFSHazardTestAccess;
};
