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
	
	FString FullPath = FPaths::ProjectContentDir() + BinaryFilePath;
	if (IFileHandle* FileHandle = FPlatformFileManager::Get().GetPlatformFile().OpenRead(*FullPath))
	{
		FileHandle->Read(reinterpret_cast<uint8*>(&TotalFrames), 4);
		FileHandle->Read(reinterpret_cast<uint8*>(&DimX), 4);
		FileHandle->Read(reinterpret_cast<uint8*>(&DimY), 4);
		FileHandle->Read(reinterpret_cast<uint8*>(&DimZ), 4);
		delete FileHandle;
		UE_LOG(LogTemp, Log, TEXT("Parsed Binary Header -> Frames: %d, DimX: %d, DimY: %d, DimZ: %d"), TotalFrames, DimX, DimY, DimZ);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to open binary file to read header: %s"), *FullPath);
		return;
	}

	// 순환 버퍼 초기화 (8000개가 아닌 400개만 할당)
	FramesBuffer.SetNum(MaxBufferSize);
	LoadedFrameIndices.Init(-1, MaxBufferSize);
	
	const int32 GridSize = DimX * DimY * DimZ;
	for (int32 i = 0; i < MaxBufferSize; i++)
	{
		FramesBuffer[i].DensityGrid.SetNumZeroed(GridSize);
		FramesBuffer[i].TemperatureGrid.SetNumZeroed(GridSize);
	}

	HeterogeneousVolume = Cast<AYUFSHeterogeneousVolume>(UGameplayStatics::GetActorOfClass(GetWorld(), AYUFSHeterogeneousVolume::StaticClass()));

	if (HeterogeneousVolume && FireWorldOrigin.IsZero())
	{
		FireWorldOrigin = HeterogeneousVolume->GetActorLocation();
		UE_LOG(LogTemp, Log, TEXT("[BinaryManager] FireWorldOrigin auto-set from HeterogeneousVolume: %s"), *FireWorldOrigin.ToString());
	}
	
	if (bDrawVoxelDebug)
	{
		GetWorld()->GetTimerManager().SetTimer(DebugTimerHandle, this, &AYUFSBinaryManager::PlayDebugAnimation, 0.1f, true);
	}
}

void AYUFSBinaryManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	if (HeterogeneousVolume)
	{
		CurrentDebugFrame = HeterogeneousVolume->GetFrame();
	}
	// 프레임 점프 감지 (에디터 조작 등)
	if (FMath::Abs(CurrentDebugFrame - LastCurrentFrame) > 50)
	{
		LoadGeneration++; // 기존의 백그라운드 로드 결과를 무효화
		bIsLoadingChunk = false; // 새로운 로드를 즉시 시작할 수 있도록 락 해제
	}
	LastCurrentFrame = CurrentDebugFrame;

	// 필요한 프레임 찾기 및 백그라운드 로드 요청
	if (!bIsLoadingChunk && TotalFrames > 0)
	{
		int32 LookBehind = 50; // 지나간 프레임 여유분
		int32 LookAhead = 200; // 다가올 프레임 미리 로드
		
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

void AYUFSBinaryManager::SetHeterogeneousVolume(AYUFSHeterogeneousVolume* InVolume)
{
	HeterogeneousVolume = InVolume;

	UE_LOG(LogTemp, Warning, TEXT("[BinaryManager] HeterogeneousVolume manually linked: %s"),
		HeterogeneousVolume ? *HeterogeneousVolume->GetName() : TEXT("NULL"));
}

void AYUFSBinaryManager::LoadDynamicChunkAsync(int32 StartFrame, int32 EndFrame, int32 Generation)
{
	if (StartFrame >= TotalFrames)
	{
		bIsLoadingChunk = false;
		return;
	}

	FString FullPath = FPaths::ProjectContentDir() + BinaryFilePath;
	int32 LocalDimX = DimX;
	int32 LocalDimY = DimY;
	int32 LocalDimZ = DimZ;

	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, FullPath, StartFrame, EndFrame, Generation, LocalDimX, LocalDimY, LocalDimZ]()
	{
		if (IFileHandle* FileHandle = FPlatformFileManager::Get().GetPlatformFile().OpenRead(*FullPath))
		{
			const int32 GridSize = LocalDimX * LocalDimY * LocalDimZ;
			const int64 BytesPerFrame = GridSize * 2; 

			int64 StartOffset = 16 + (StartFrame * BytesPerFrame);
			FileHandle->Seek(StartOffset); 

			// GameThread에 덮어쓰기 위해 임시 배열에 할당
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

			// 메인 스레드로 넘겨서 버퍼 갱신 (MoveTemp 사용)
			AsyncTask(ENamedThreads::GameThread, [this, StartFrame, EndFrame, Generation, TempData = MoveTemp(TempFrames)]() mutable
			{
				// 해당 로드 작업이 취소/무효화되지 않은 경우에만 버퍼에 덮어쓰기
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
	if (CurrentDebugFrame >= TotalFrames)
	{
		return; 
	}
	
	// 아직 로드되지 않은 프레임이면 그리지 않음
	if (LoadedFrameIndices[CurrentDebugFrame % MaxBufferSize] != CurrentDebugFrame)
	{
		return;
	}

	FlushPersistentDebugLines(GetWorld());

	int32 SafeDebugStep = FMath::Max(DebugStep, 4); 

	for (int32 x = 0; x < DimX; x += SafeDebugStep)
	{
		for (int32 y = 0; y < DimY; y += SafeDebugStep)
		{
			for (int32 z = 0; z < DimZ; z += SafeDebugStep)
			{
				int32 FlatIndex = (x * DimY * DimZ) + (y * DimZ) + z;

				// 하드코딩된 GetSmokeDensityAtLocation의 완벽한 역연산
				FVector WorldPos(
					-x * VoxelSize,
					-((DimY - 1) - y) * VoxelSize,
					z * VoxelSize
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
	
	if (FrameIndex < 0 || FrameIndex >= TotalFrames) 
	{
		return false;
	}

	// 로드되지 않은 프레임이면 안전하게 false 반환 (데이터 없음)
	if (LoadedFrameIndices[FrameIndex % MaxBufferSize] != FrameIndex)
	{
		return false;
	}

	FVector Local = WorldLocation - FireWorldOrigin;
	float LocalX = -Local.X;
	float LocalY = -Local.Y;
	float LocalZ = Local.Z;

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

	if (FrameIndex < 0 || FrameIndex >= TotalFrames)
	{
		return false;
	}

	if (LoadedFrameIndices[FrameIndex % MaxBufferSize] != FrameIndex)
	{
		return false;
	}

	FVector Local = WorldLocation - FireWorldOrigin;
	float LocalX = -Local.X;
	float LocalY = -Local.Y;
	float LocalZ = Local.Z;

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
