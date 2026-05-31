// Fill out your copyright notice in the Description page of Project Settings.

#include "YUFSBinaryManager.h"
#include "YUFSHeterogeneousVolume.h"
#include "Kismet/GameplayStatics.h"
#include "Async/Async.h"

AYUFSBinaryManager::AYUFSBinaryManager()
{
	PrimaryActorTick.bCanEverTick = true;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
	RootComponent = Root;
}

void AYUFSBinaryManager::BeginPlay()
{
	Super::BeginPlay();

	HeterogeneousVolume = Cast<AYUFSHeterogeneousVolume>(
		UGameplayStatics::GetActorOfClass(GetWorld(), AYUFSHeterogeneousVolume::StaticClass()));

	if (!HeterogeneousVolume)
	{
		UE_LOG(LogTemp, Error, TEXT("[YUFSBinaryManager] HeterogeneousVolume not found in level."));
	}

	// 디버그 드로우는 InitializeForScenario() 이후에도 동작하도록 미리 등록
	// TotalFrames == 0인 동안은 PlayDebugAnimation() 내부 조건에서 조기 반환
	if (bDrawVoxelDebug)
	{
		GetWorld()->GetTimerManager().SetTimer(
			DebugTimerHandle, this, &AYUFSBinaryManager::PlayDebugAnimation, 0.1f, true);
	}
}

void AYUFSBinaryManager::InitializeForScenario()
{
	if (!HeterogeneousVolume)
	{
		UE_LOG(LogTemp, Error, TEXT("[YUFSBinaryManager] InitializeForScenario: HeterogeneousVolume is null."));
		return;
	}

	ActiveBinaryPath = HeterogeneousVolume->GetActiveScenarioBinaryPath();
	if (ActiveBinaryPath.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[YUFSBinaryManager] InitializeForScenario: BinaryDataPath is empty. Check FireScenarios config."));
		return;
	}

	WorldOrigin = HeterogeneousVolume->GetActiveScenarioWorldOrigin();

	// 헤더 파싱 (Frames, DimX, DimY, DimZ)
	const FString FullPath = FPaths::ProjectContentDir() + ActiveBinaryPath;
	if (IFileHandle* FileHandle = FPlatformFileManager::Get().GetPlatformFile().OpenRead(*FullPath))
	{
		FileHandle->Read(reinterpret_cast<uint8*>(&TotalFrames), 4);
		FileHandle->Read(reinterpret_cast<uint8*>(&DimX), 4);
		FileHandle->Read(reinterpret_cast<uint8*>(&DimY), 4);
		FileHandle->Read(reinterpret_cast<uint8*>(&DimZ), 4);
		delete FileHandle;
		UE_LOG(LogTemp, Log, TEXT("[YUFSBinaryManager] Initialized | Frames: %d | Dim: (%d,%d,%d) | Path: %s"),
			TotalFrames, DimX, DimY, DimZ, *ActiveBinaryPath);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[YUFSBinaryManager] Failed to open binary file: %s"), *FullPath);
		return;
	}

	// 기존 로드 작업 무효화 후 버퍼 재초기화
	LoadGeneration++;
	bIsLoadingChunk = false;
	LastCurrentFrame = -1;

	FramesBuffer.SetNum(MaxBufferSize);
	LoadedFrameIndices.Init(-1, MaxBufferSize);

	const int32 GridSize = DimX * DimY * DimZ;
	for (int32 i = 0; i < MaxBufferSize; i++)
	{
		FramesBuffer[i].DensityGrid.SetNumZeroed(GridSize);
		FramesBuffer[i].TemperatureGrid.SetNumZeroed(GridSize);
	}
}

void AYUFSBinaryManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (HeterogeneousVolume)
	{
		CurrentDebugFrame = HeterogeneousVolume->GetFrame();
	}

	// 초기화 전이면 스트리밍 하지 않음
	if (TotalFrames == 0) return;

	// 프레임 점프 감지 (에디터 조작 등)
	if (FMath::Abs(CurrentDebugFrame - LastCurrentFrame) > 50)
	{
		LoadGeneration++;
		bIsLoadingChunk = false;
	}
	LastCurrentFrame = CurrentDebugFrame;

	if (!bIsLoadingChunk)
	{
		const int32 LookBehind = 50;
		const int32 LookAhead = 200;

		int32 StartF = FMath::Max(0, CurrentDebugFrame - LookBehind);
		int32 EndF = FMath::Min(TotalFrames, CurrentDebugFrame + LookAhead);

		int32 MissingStart = -1;
		for (int32 f = StartF; f < EndF; ++f)
		{
			if (LoadedFrameIndices[f % MaxBufferSize] != f)
			{
				MissingStart = f;
				break;
			}
		}

		if (MissingStart != -1)
		{
			int32 MissingEnd = FMath::Min(MissingStart + ChunkSize, EndF);
			bIsLoadingChunk = true;
			LoadDynamicChunkAsync(MissingStart, MissingEnd, LoadGeneration);
		}
	}
}

void AYUFSBinaryManager::LoadDynamicChunkAsync(int32 StartFrame, int32 EndFrame, int32 Generation)
{
	if (StartFrame >= TotalFrames)
	{
		bIsLoadingChunk = false;
		return;
	}

	const FString FullPath = FPaths::ProjectContentDir() + ActiveBinaryPath;
	int32 LocalDimX = DimX;
	int32 LocalDimY = DimY;
	int32 LocalDimZ = DimZ;

	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
		[this, FullPath, StartFrame, EndFrame, Generation, LocalDimX, LocalDimY, LocalDimZ]()
	{
		if (IFileHandle* FileHandle = FPlatformFileManager::Get().GetPlatformFile().OpenRead(*FullPath))
		{
			const int32 GridSize = LocalDimX * LocalDimY * LocalDimZ;
			const int64 BytesPerFrame = GridSize * 2;

			FileHandle->Seek(16 + (StartFrame * BytesPerFrame));

			TArray<FFrameData> TempFrames;
			TempFrames.SetNum(EndFrame - StartFrame);

			for (int32 i = 0; i < EndFrame - StartFrame; i++)
			{
				TempFrames[i].DensityGrid.SetNumUninitialized(GridSize);
				TempFrames[i].TemperatureGrid.SetNumUninitialized(GridSize);
				FileHandle->Read(TempFrames[i].DensityGrid.GetData(), GridSize);
				FileHandle->Read(TempFrames[i].TemperatureGrid.GetData(), GridSize);
			}
			delete FileHandle;

			AsyncTask(ENamedThreads::GameThread,
				[this, StartFrame, EndFrame, Generation, TempData = MoveTemp(TempFrames)]() mutable
			{
				if (Generation == this->LoadGeneration)
				{
					for (int32 i = 0; i < EndFrame - StartFrame; i++)
					{
						int32 f = StartFrame + i;
						int32 Idx = f % MaxBufferSize;
						this->FramesBuffer[Idx] = MoveTemp(TempData[i]);
						this->LoadedFrameIndices[Idx] = f;
					}
				}
				this->bIsLoadingChunk = false;
			});
		}
		else
		{
			AsyncTask(ENamedThreads::GameThread, [this]() { this->bIsLoadingChunk = false; });
		}
	});
}

void AYUFSBinaryManager::PlayDebugAnimation()
{
	if (TotalFrames == 0 || CurrentDebugFrame >= TotalFrames) return;

	if (LoadedFrameIndices[CurrentDebugFrame % MaxBufferSize] != CurrentDebugFrame) return;

	FlushPersistentDebugLines(GetWorld());

	int32 SafeDebugStep = FMath::Max(DebugStep, 4);

	for (int32 x = 0; x < DimX; x += SafeDebugStep)
	{
		for (int32 y = 0; y < DimY; y += SafeDebugStep)
		{
			for (int32 z = 0; z < DimZ; z += SafeDebugStep)
			{
				int32 FlatIndex = (x * DimY * DimZ) + (y * DimZ) + z;

				FVector WorldPos(
					-x * VoxelSize + WorldOrigin.X,
					-((DimY - 1) - y) * VoxelSize + WorldOrigin.Y,
					z * VoxelSize + WorldOrigin.Z
				);

				FVector BaseExtent = FVector(VoxelSize * 0.5f * SafeDebugStep);

				uint8 DensityValue = FramesBuffer[CurrentDebugFrame % MaxBufferSize].DensityGrid[FlatIndex];
				if (DensityValue > DensityThreshold)
				{
					DrawDebugBox(GetWorld(), WorldPos, BaseExtent, FQuat::Identity, DensityColor, true, -1.0f, 0, 1.0f);
				}

				uint8 TemperatureValue = FramesBuffer[CurrentDebugFrame % MaxBufferSize].TemperatureGrid[FlatIndex];
				if (TemperatureValue > TemperatureThreshold)
				{
					FVector TempExtent = BaseExtent * 1.1f;
					DrawDebugBox(GetWorld(), WorldPos, TempExtent, FQuat::Identity, TemperatureColor, true, -1.0f, 0, 1.0f);
				}
			}
		}
	}
}

bool AYUFSBinaryManager::GetSmokeDensityAtLocation(FVector WorldLocation, int32 FrameIndex, uint8& OutDensity)
{
	OutDensity = 0;

	if (TotalFrames == 0 || FrameIndex < 0 || FrameIndex >= TotalFrames) return false;

	if (LoadedFrameIndices[FrameIndex % MaxBufferSize] != FrameIndex) return false;

	float LocalX = -(WorldLocation.X - WorldOrigin.X);
	float LocalY = -(WorldLocation.Y - WorldOrigin.Y);
	float LocalZ = WorldLocation.Z - WorldOrigin.Z;

	int32 IndexX = FMath::FloorToInt(LocalX / VoxelSize);
	int32 BaseIndexY = FMath::FloorToInt(LocalY / VoxelSize);
	int32 IndexZ = FMath::FloorToInt(LocalZ / VoxelSize);
	int32 IndexY = (DimY - 1) - BaseIndexY;

	if (IndexX >= 0 && IndexX < DimX &&
		IndexY >= 0 && IndexY < DimY &&
		IndexZ >= 0 && IndexZ < DimZ)
	{
		int32 FlatIndex = (IndexX * DimY * DimZ) + (IndexY * DimZ) + IndexZ;
		OutDensity = FramesBuffer[FrameIndex % MaxBufferSize].DensityGrid[FlatIndex];
		return true;
	}

	return false;
}

bool AYUFSBinaryManager::GetTemperatureAtLocation(FVector WorldLocation, int32 FrameIndex, uint8& OutTemperature)
{
	OutTemperature = 0;

	if (TotalFrames == 0 || FrameIndex < 0 || FrameIndex >= TotalFrames) return false;

	if (LoadedFrameIndices[FrameIndex % MaxBufferSize] != FrameIndex) return false;

	float LocalX = -(WorldLocation.X - WorldOrigin.X);
	float LocalY = -(WorldLocation.Y - WorldOrigin.Y);
	float LocalZ = WorldLocation.Z - WorldOrigin.Z;

	int32 IndexX = FMath::FloorToInt(LocalX / VoxelSize);
	int32 BaseIndexY = FMath::FloorToInt(LocalY / VoxelSize);
	int32 IndexZ = FMath::FloorToInt(LocalZ / VoxelSize);
	int32 IndexY = (DimY - 1) - BaseIndexY;

	if (IndexX >= 0 && IndexX < DimX &&
		IndexY >= 0 && IndexY < DimY &&
		IndexZ >= 0 && IndexZ < DimZ)
	{
		int32 FlatIndex = (IndexX * DimY * DimZ) + (IndexY * DimZ) + IndexZ;
		OutTemperature = FramesBuffer[FrameIndex % MaxBufferSize].TemperatureGrid[FlatIndex];
		return true;
	}

	return false;
}
