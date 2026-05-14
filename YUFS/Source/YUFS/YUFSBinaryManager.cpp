// Fill out your copyright notice in the Description page of Project Settings.


#include "YUFSBinaryManager.h"

#include "YUFSHeterogeneousVolume.h"
#include "Kismet/GameplayStatics.h"

// Sets default values
AYUFSBinaryManager::AYUFSBinaryManager()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
    RootComponent = Root;

}

// Called when the game starts or when spawned
void AYUFSBinaryManager::BeginPlay()
{
	Super::BeginPlay();
	
	FramesData.SetNum(TotalFrames);
	const int32 GridSize = DimX * DimY * DimZ;

	for (int32 f = 0; f < TotalFrames; f++)
	{
		FramesData[f].DensityGrid.SetNumZeroed(GridSize);
		FramesData[f].TemperatureGrid.SetNumZeroed(GridSize);
	}

	HeterogeneousVolume = Cast<AYUFSHeterogeneousVolume>(UGameplayStatics::GetActorOfClass(GetWorld(), AYUFSHeterogeneousVolume::StaticClass()));
	
	LoadedFramesCount.Reset();
	LoadChunkAsync(0, ChunkSize);
    
	GetWorld()->GetTimerManager().SetTimer(DebugTimerHandle, this, &AYUFSBinaryManager::PlayDebugAnimation, 0.1f, true);
	
}

// Called every frame
void AYUFSBinaryManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (HeterogeneousVolume)
	{
		CurrentDebugFrame = HeterogeneousVolume->GetFrame();
	}
}

void AYUFSBinaryManager::PlayDebugAnimation()
{
	int32 LoadedCount = LoadedFramesCount.GetValue();

	if (CurrentDebugFrame >= LoadedCount || CurrentDebugFrame >= TotalFrames)
	{
		return; 
	}
	
	FlushPersistentDebugLines(GetWorld());

	FTransform ActorTransform = GetActorTransform();
	FQuat ActorRotation = ActorTransform.GetRotation();
	
	int32 SafeDebugStep = FMath::Max(DebugStep, 4); 

	for (int32 x = 0; x < DimX; x += SafeDebugStep)
	{
		for (int32 y = 0; y < DimY; y += SafeDebugStep)
		{
			for (int32 z = 0; z < DimZ; z += SafeDebugStep)
			{
				int32 FlatIndex = (x * DimY * DimZ) + (y * DimZ) + z;

				FVector LocalPos = FVector(y * VoxelSize, x * VoxelSize, z * VoxelSize);
				FVector WorldPos = ActorTransform.TransformPosition(LocalPos);
				FVector BaseExtent = FVector(VoxelSize * 0.5f * SafeDebugStep);

				uint8 DensityValue = FramesData[CurrentDebugFrame].DensityGrid[FlatIndex];
				if (DensityValue > DensityThreshold) 
				{
					DrawDebugBox(GetWorld(), WorldPos, BaseExtent, ActorRotation, DensityColor, true, -1.0f, 0, 1.0f);
				}

				uint8 TemperatureValue = FramesData[CurrentDebugFrame].TemperatureGrid[FlatIndex];
				if (TemperatureValue > TemperatureThreshold)
				{
					FVector TempExtent = BaseExtent * 1.1f; 
					DrawDebugBox(GetWorld(), WorldPos, TempExtent, ActorRotation, TemperatureColor, true, -1.0f, 0, 1.0f);
				}
			}
		}
	}
}

bool AYUFSBinaryManager::GetSmokeDensityAtLocation(FVector WorldLocation, int32 FrameIndex, uint8& OutDensity)
{
	OutDensity = 0;
	
	if (FrameIndex < 0 || FrameIndex >= TotalFrames || FrameIndex >= LoadedFramesCount.GetValue()) 
	{
		return false;
	}

	float LocalX = -WorldLocation.X; 
	float LocalY = -WorldLocation.Y; 
	float LocalZ = WorldLocation.Z;

	int32 IndexX = FMath::FloorToInt(LocalX / VoxelSize);
	int32 BaseIndexY = FMath::FloorToInt(LocalY / VoxelSize);
	int32 IndexZ = FMath::FloorToInt(LocalZ / VoxelSize);
	int32 IndexY = (DimY - 1) - BaseIndexY;

	if (IndexX >= 0 && IndexX < DimX &&
		IndexY >= 0 && IndexY < DimY &&
		IndexZ >= 0 && IndexZ < DimZ)
	{
		int32 FlatIndex = (IndexX * DimY * DimZ) + (IndexY * DimZ) + IndexZ;
        
		OutDensity = FramesData[FrameIndex].DensityGrid[FlatIndex];
		return true;
	}

	return false;
}

bool AYUFSBinaryManager::GetTemperatureAtLocation(FVector WorldLocation, int32 FrameIndex, uint8& OutTemperature)
{
	OutTemperature = 0;
	
	if (FrameIndex < 0 || FrameIndex >= TotalFrames || FrameIndex >= LoadedFramesCount.GetValue()) 
	{
		return false;
	}

	float LocalX = -WorldLocation.X; 
	float LocalY = -WorldLocation.Y; 
	float LocalZ = WorldLocation.Z;

	int32 IndexX = FMath::FloorToInt(LocalX / VoxelSize);
	int32 BaseIndexY = FMath::FloorToInt(LocalY / VoxelSize);
	int32 IndexZ = FMath::FloorToInt(LocalZ / VoxelSize);
	int32 IndexY = (DimY - 1) - BaseIndexY;

	if (IndexX >= 0 && IndexX < DimX &&
		IndexY >= 0 && IndexY < DimY &&
		IndexZ >= 0 && IndexZ < DimZ)
	{
		int32 FlatIndex = (IndexX * DimY * DimZ) + (IndexY * DimZ) + IndexZ;
        
		OutTemperature = FramesData[FrameIndex].TemperatureGrid[FlatIndex];
		return true;
	}

	return false;
}

void AYUFSBinaryManager::LoadChunkAsync(int32 StartFrame, int32 EndFrame)
{
	if (StartFrame >= TotalFrames) return;
	int32 SafeEndFrame = FMath::Min(EndFrame, TotalFrames);

	FString FullPath = FPaths::ProjectContentDir() + TEXT("Fires/FirePrototype/BinaryData/smoke_data.bin");

	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, FullPath, StartFrame, SafeEndFrame]()
	{
		if (IFileHandle* FileHandle = FPlatformFileManager::Get().GetPlatformFile().OpenRead(*FullPath))
		{
			const int32 GridSize = DimX * DimY * DimZ;
			const int64 BytesPerFrame = GridSize * 2; // Density + Temperature

			int64 StartOffset = 16 + (StartFrame * BytesPerFrame);
			FileHandle->Seek(StartOffset); 

			for (int32 f = StartFrame; f < SafeEndFrame; f++)
			{
				FileHandle->Read(FramesData[f].DensityGrid.GetData(), GridSize);
				FileHandle->Read(FramesData[f].TemperatureGrid.GetData(), GridSize);

				LoadedFramesCount.Increment();
			}
			delete FileHandle;
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Async File Open FAILED: %s"), *FullPath);
			return;
		}

		AsyncTask(ENamedThreads::GameThread, [this, SafeEndFrame]()
		{
			if (SafeEndFrame < TotalFrames)
			{
				LoadChunkAsync(SafeEndFrame, SafeEndFrame + ChunkSize);
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("All %d frames loaded successfully!"), TotalFrames);
			}
		});
	});
}

