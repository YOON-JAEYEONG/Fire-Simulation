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

	// 지정한 .bin 파일(Content 폴더 기준 상대 경로)을 다시 읽어 재생 데이터를 통째로 교체합니다.
	// 헤더 파싱에 실패하면 false를 반환하고 이전 데이터는 무효화됩니다(bHeaderValid=false).
	// 레벨 리로드 없이 콤보박스로 화재를 전환할 때 SimulationController가 호출합니다.
	UFUNCTION(BlueprintCallable, Category="Fire")
	bool LoadBinaryFile(const FString& NewRelativePath);

	bool GetSmokeDensityAtLocation(FVector WorldLocation, int32 FrameIndex, uint8& OutDensity);
	bool GetTemperatureAtLocation(FVector WorldLocation, int32 FrameIndex, uint8& OutTemperature);
	FYUFSHazardSnapshot GetHazardSnapshot(int32 FrameIndex) const;
	UFUNCTION(BlueprintPure, Category="Fire|Diagnostics")
	EYUFSHazardDataStatus GetDataStatus() const { return GetHazardSnapshot(CurrentDebugFrame).Status; }
	UFUNCTION(BlueprintPure, Category="Fire|Diagnostics")
	FString GetHazardDiagnostics() const;
	
	int32 GetCurrentFrame() const { return CurrentDebugFrame; }
	// Provenance gate for optional FDS-targeted interactions; navigation keeps the JJW diagnostics contract.
	bool IsDatasetAlignmentConfirmed() const { return bDatasetAlignmentConfirmed; }
	AYUFSHeterogeneousVolume* GetHeterogeneousVolume() const { return HeterogeneousVolume; }

	// ── 건물(월드) 고정 격자 ─────────────────────────────────────────────
	// .bin 격자는 FDS 전체 도메인이라 같은 건물의 화재라면 월드에서 항상 같은 자리여야 합니다.
	// 반면 vdb(SVT)는 연기가 있는 범위로 경계가 잡혀 화재마다 import 원점이 달라지고, 그래서
	// 화재별 VolumeTransform도 달라집니다. 이 함수로 "기준 화재"의 볼륨 Transform을 넘겨주면
	// 이후 격자는 현재 볼륨 Transform이 아니라 이 기준 Transform으로 계산되어 고정됩니다.
	// (GridOriginLocal/VoxelSize는 그대로 기준 화재에 맞춰 둔 값이 적용됩니다.)
	void SetWorldAnchoredGrid(const FTransform& ReferenceVolumeTransform);
	// 예전 방식(격자가 현재 볼륨 Transform을 따라감)으로 되돌립니다.
	void ClearWorldAnchoredGrid();
	bool IsGridWorldAnchored() const { return bGridWorldAnchored; }

private:
	void LoadDynamicChunkAsync(int32 StartFrame, int32 EndFrame, int32 Generation);
	// VolumeTransform(볼륨 컴포넌트의 월드 Transform) 기준으로 격자→월드 Transform을 계산합니다.
	bool ComputeGridToWorld(const FTransform& VolumeTransform, FTransform& OutGridToWorld) const;

	bool bGridWorldAnchored = false;
	FTransform GridAnchorVolumeTransform = FTransform::Identity;

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
