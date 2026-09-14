// Fill out your copyright notice in the Description page of Project Settings.

#include "YUFSBinaryManager.h"
#include "YUFSHeterogeneousVolume.h"
#include "Kismet/GameplayStatics.h"
#include "Async/Async.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "DrawDebugHelpers.h"

namespace
{
	constexpr int64 BinaryHeaderBytes = 16;
}

AYUFSBinaryManager::AYUFSBinaryManager()
{
	PrimaryActorTick.bCanEverTick = true;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
    RootComponent = Root;
}

void AYUFSBinaryManager::BeginPlay()
{
	Super::BeginPlay();
	if (ActiveLoadCancellation) ActiveLoadCancellation->store(true, std::memory_order_relaxed);
	ActiveLoadCancellation.Reset();
	++LoadGeneration;
	bStreamingActive = false;
	bIsLoadingChunk = false;
	TotalFrames = DimX = DimY = DimZ = GridSizeBytes = 0;
	ValidatedFileSize = 0;
	LastCurrentFrame = -1;
	NextLoadRetryTime = 0.0;
	FramesBuffer.Reset();
	LoadedFrameIndices.Reset();
	ValidatedBinaryPath = FPaths::ProjectContentDir() + BinaryFilePath;
	TUniquePtr<IFileHandle> FileHandle(FPlatformFileManager::Get().GetPlatformFile().OpenRead(*ValidatedBinaryPath));
	if (!FileHandle)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to open binary file to read header: %s"), *ValidatedBinaryPath);
		return;
	}
	int32 Header[4] = {};
	const int64 FileSize = FileHandle->Size();
	if (FileSize < BinaryHeaderBytes || !FileHandle->Read(reinterpret_cast<uint8*>(Header), BinaryHeaderBytes)
		|| Header[0] <= 0 || Header[1] <= 0 || Header[2] <= 0 || Header[3] <= 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[BinaryManager] Invalid or truncated binary header; no dataset is published."));
		return;
	}
	// Validate products before allocation. Keep the existing packed uint8 density + temperature format.
	const int64 XY = static_cast<int64>(Header[1]) * Header[2];
	if (XY > static_cast<int64>(MAX_int32) / Header[3])
	{
		UE_LOG(LogTemp, Error, TEXT("[BinaryManager] Grid dimensions exceed supported array size."));
		return;
	}
	const int32 GridSize = static_cast<int32>(XY * Header[3]);
	const int64 BytesPerFrame = static_cast<int64>(GridSize) * 2;
	if (Header[0] > (FileSize - BinaryHeaderBytes) / BytesPerFrame
		|| BinaryHeaderBytes + static_cast<int64>(Header[0]) * BytesPerFrame != FileSize)
	{
		UE_LOG(LogTemp, Error, TEXT("[BinaryManager] Header dimensions/frame count do not match payload size; refusing partial data."));
		return;
	}
	if (ChunkSize <= 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[BinaryManager] ChunkSize must be positive."));
		return;
	}
	const uint64 RequiredBytes = static_cast<uint64>(BytesPerFrame) * (static_cast<uint64>(MaxBufferSize) + ChunkSize);
	const uint64 AvailableBytes = FPlatformMemory::GetStats().AvailablePhysical;
	if (AvailableBytes > 0 && RequiredBytes > AvailableBytes)
	{
		UE_LOG(LogTemp, Error, TEXT("[BinaryManager] Valid dataset cannot fit its ring buffer and one chunk in available physical memory."));
		return;
	}
	TotalFrames = Header[0]; DimX = Header[1]; DimY = Header[2]; DimZ = Header[3];
	GridSizeBytes = GridSize;
	ValidatedFileSize = FileSize;
	FileHandle.Reset();
	UE_LOG(LogTemp, Log, TEXT("Parsed Binary Header -> Frames: %d, DimX: %d, DimY: %d, DimZ: %d"), TotalFrames, DimX, DimY, DimZ);

	// Existing bounded streaming policy: 192 ring slots and 24 frames per default chunk.
	FramesBuffer.SetNum(MaxBufferSize);
	LoadedFrameIndices.Init(-1, MaxBufferSize);
	for (int32 i = 0; i < MaxBufferSize; i++)
	{
		FramesBuffer[i].DensityGrid.SetNumUninitialized(GridSize);
		FramesBuffer[i].TemperatureGrid.SetNumUninitialized(GridSize);
	}
	bStreamingActive = true;

	HeterogeneousVolume = Cast<AYUFSHeterogeneousVolume>(UGameplayStatics::GetActorOfClass(GetWorld(), AYUFSHeterogeneousVolume::StaticClass()));

	if (bDrawVoxelDebug)
	{
		GetWorld()->GetTimerManager().SetTimer(DebugTimerHandle, this, &AYUFSBinaryManager::PlayDebugAnimation, 0.1f, true);
	}
}

void AYUFSBinaryManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Workers only own scalar snapshots. Late completions cannot publish into this actor after EndPlay.
	bStreamingActive = false;
	if (ActiveLoadCancellation) ActiveLoadCancellation->store(true, std::memory_order_relaxed);
	ActiveLoadCancellation.Reset();
	++LoadGeneration;
	bIsLoadingChunk = false;
	GetWorldTimerManager().ClearTimer(DebugTimerHandle);
	FramesBuffer.Reset();
	LoadedFrameIndices.Reset();
	TotalFrames = 0;
	Super::EndPlay(EndPlayReason);
}

void AYUFSBinaryManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bStreamingActive) return;
	if (IsValid(HeterogeneousVolume))
	{
		CurrentDebugFrame = HeterogeneousVolume->GetFrame();
	}
	// 프레임 점프 감지 (에디터 조작 등)
	if (FMath::Abs(static_cast<int64>(CurrentDebugFrame) - LastCurrentFrame) > 50)
	{
		if (ActiveLoadCancellation) ActiveLoadCancellation->store(true, std::memory_order_relaxed);
		LoadGeneration++; // 기존의 백그라운드 로드 결과를 무효화
		bIsLoadingChunk = false; // 새로운 로드를 즉시 시작할 수 있도록 락 해제
	}
	LastCurrentFrame = CurrentDebugFrame;
	if (CurrentDebugFrame < 0 || CurrentDebugFrame >= TotalFrames) return;

	// 필요한 프레임 찾기 및 백그라운드 로드 요청
	if (!bIsLoadingChunk && TotalFrames > 0 && FPlatformTime::Seconds() >= NextLoadRetryTime)
	{
		int32 LookBehind = 24; // 지나간 프레임 여유분
		int32 LookAhead = 120; // 다가올 프레임 미리 로드
		
		int32 StartF = FMath::Max(0, CurrentDebugFrame - LookBehind);
		int32 EndF = static_cast<int32>(FMath::Min(static_cast<int64>(TotalFrames), static_cast<int64>(CurrentDebugFrame) + LookAhead));

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
			int32 MissingEnd = static_cast<int32>(FMath::Min(static_cast<int64>(MissingStart) + ChunkSize, static_cast<int64>(EndF)));
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

void AYUFSBinaryManager::LoadDynamicChunkAsync(int32 StartFrame, int32 EndFrame, uint64 Generation)
{
	if (!bStreamingActive || Generation != LoadGeneration) return;
	if (StartFrame < 0 || StartFrame >= TotalFrames || EndFrame <= StartFrame || EndFrame > TotalFrames)
	{
		bIsLoadingChunk = false;
		return;
	}

	const FString FullPath = ValidatedBinaryPath;
	const int32 LocalDimX = DimX, LocalDimY = DimY, LocalDimZ = DimZ;
	const int32 LocalTotalFrames = TotalFrames, GridSize = GridSizeBytes;
	const int64 ExpectedFileSize = ValidatedFileSize;
	const TWeakObjectPtr<AYUFSBinaryManager> WeakThis(this);
	const TSharedRef<std::atomic<bool>, ESPMode::ThreadSafe> Cancellation = MakeShared<std::atomic<bool>, ESPMode::ThreadSafe>(false);
	ActiveLoadCancellation = Cancellation;
	// Never dereference a UObject on the worker. EndPlay/seek invalidation is checked on the game thread.
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
		[WeakThis, Cancellation, FullPath, StartFrame, EndFrame, Generation, LocalDimX, LocalDimY, LocalDimZ, LocalTotalFrames, GridSize, ExpectedFileSize]()
	{
		if (Cancellation->load(std::memory_order_relaxed)) return;
		TArray<FFrameData> TempFrames;
		bool bReadComplete = false;
		TUniquePtr<IFileHandle> FileHandle(FPlatformFileManager::Get().GetPlatformFile().OpenRead(*FullPath));
		if (FileHandle)
		{
			int32 Header[4] = {};
			const int64 BytesPerFrame = static_cast<int64>(GridSize) * 2;
			const int64 StartOffset = BinaryHeaderBytes + static_cast<int64>(StartFrame) * BytesPerFrame;
			// Reopen must still match the validated dataset, not a truncated/replaced header.
			bReadComplete = FileHandle->Size() == ExpectedFileSize
				&& FileHandle->Read(reinterpret_cast<uint8*>(Header), BinaryHeaderBytes)
				&& Header[0] == LocalTotalFrames && Header[1] == LocalDimX && Header[2] == LocalDimY && Header[3] == LocalDimZ
				&& FileHandle->Seek(StartOffset);
			if (bReadComplete) TempFrames.SetNum(EndFrame - StartFrame);
			for (int32 i = 0; bReadComplete && i < TempFrames.Num(); ++i)
			{
				if (Cancellation->load(std::memory_order_relaxed)) return;
				TempFrames[i].DensityGrid.SetNumUninitialized(GridSize);
				TempFrames[i].TemperatureGrid.SetNumUninitialized(GridSize);
				bReadComplete = FileHandle->Read(TempFrames[i].DensityGrid.GetData(), GridSize)
					&& FileHandle->Read(TempFrames[i].TemperatureGrid.GetData(), GridSize);
			}
		}
		FileHandle.Reset();
		if (Cancellation->load(std::memory_order_relaxed)) return;
		if (!bReadComplete) TempFrames.Reset(); // No uninitialized or partial bytes may be published.

		AsyncTask(ENamedThreads::GameThread,
			[WeakThis, StartFrame, EndFrame, Generation, GridSize, bReadComplete, TempData = MoveTemp(TempFrames)]() mutable
		{
			AYUFSBinaryManager* Manager = WeakThis.Get();
			if (!Manager || !Manager->bStreamingActive || Generation != Manager->LoadGeneration) return;
			// Only this request's generation can release its lock. Stale failures also return above.
			Manager->bIsLoadingChunk = false;
			Manager->ActiveLoadCancellation.Reset();
			if (!bReadComplete || TempData.Num() != EndFrame - StartFrame)
			{
				Manager->LoadedFrameIndices.Init(-1, Manager->MaxBufferSize);
				Manager->NextLoadRetryTime = FPlatformTime::Seconds() + 1.0;
				UE_LOG(LogTemp, Warning, TEXT("[BinaryManager] Chunk read failed or file changed; no frames published (retry after 1 second)."));
				return;
			}
			if (Manager->FramesBuffer.Num() != Manager->MaxBufferSize || Manager->LoadedFrameIndices.Num() != Manager->MaxBufferSize) return;
			// Validate the whole transfer before writing any ring slot.
			for (int32 i = 0; i < TempData.Num(); ++i)
			{
				const FFrameData& Destination = Manager->FramesBuffer[(StartFrame + i) % Manager->MaxBufferSize];
				if (TempData[i].DensityGrid.Num() != GridSize || TempData[i].TemperatureGrid.Num() != GridSize
					|| Destination.DensityGrid.Num() != GridSize || Destination.TemperatureGrid.Num() != GridSize) return;
			}
			// Retain preallocated slot capacity: publication is game-thread only and copy-based.
			for (int32 i = 0; i < TempData.Num(); ++i)
			{
				const int32 Frame = StartFrame + i;
				const int32 Slot = Frame % Manager->MaxBufferSize;
				FFrameData& Destination = Manager->FramesBuffer[Slot];
				FMemory::Memcpy(Destination.DensityGrid.GetData(), TempData[i].DensityGrid.GetData(), GridSize);
				FMemory::Memcpy(Destination.TemperatureGrid.GetData(), TempData[i].TemperatureGrid.GetData(), GridSize);
				Manager->LoadedFrameIndices[Slot] = Frame;
			}
			Manager->NextLoadRetryTime = 0.0;
		});
	});
}

void AYUFSBinaryManager::PlayDebugAnimation()
{
	if (!bStreamingActive || !bDatasetAlignmentConfirmed || CurrentDebugFrame < 0 || CurrentDebugFrame >= TotalFrames)
	{
		return; 
	}
	
	// 아직 로드되지 않은 프레임이면 그리지 않음
	if (LoadedFrameIndices[CurrentDebugFrame % MaxBufferSize] != CurrentDebugFrame)
	{
		return;
	}

	if (!IsValid(HeterogeneousVolume)) return;
	const FVector VolumeScale = HeterogeneousVolume->GetActorScale3D();
	if (VolumeScale.ContainsNaN() || FMath::IsNearlyZero(VolumeScale.X)
		|| FMath::IsNearlyZero(VolumeScale.Y) || FMath::IsNearlyZero(VolumeScale.Z)
		|| !FMath::IsFinite(VoxelSize) || VoxelSize <= 0.f) return;

	FlushPersistentDebugLines(GetWorld());

	const float LocalVoxelSize = VoxelSize / FMath::Abs(HeterogeneousVolume->GetActorScale3D().X);
	int32 SafeDebugStep = FMath::Max(DebugStep, 4);

	for (int32 x = 0; x < DimX; x += SafeDebugStep)
	{
		for (int32 y = 0; y < DimY; y += SafeDebugStep)
		{
			for (int32 z = 0; z < DimZ; z += SafeDebugStep)
			{
				int32 FlatIndex = (x * DimY * DimZ) + (y * DimZ) + z;

							FVector LocalPos(x * LocalVoxelSize, y * LocalVoxelSize, z * LocalVoxelSize);
				FVector WorldPos = HeterogeneousVolume->GetActorTransform().TransformPosition(LocalPos);

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

bool AYUFSBinaryManager::ResolveLoadedGridIndex(const FVector& WorldLocation, int32 FrameIndex, int32& OutSlot, int32& OutFlatIndex) const
{
	OutSlot = OutFlatIndex = INDEX_NONE;
	if (!bStreamingActive || !bDatasetAlignmentConfirmed || FrameIndex < 0 || FrameIndex >= TotalFrames
		|| !IsValid(HeterogeneousVolume) || WorldLocation.ContainsNaN()) return false;
	const int32 Slot = FrameIndex % MaxBufferSize;
	if (!LoadedFrameIndices.IsValidIndex(Slot) || !FramesBuffer.IsValidIndex(Slot)
		|| LoadedFrameIndices[Slot] != FrameIndex) return false;
	const FVector VolumeScale = HeterogeneousVolume->GetActorScale3D();
	if (VolumeScale.ContainsNaN() || FMath::IsNearlyZero(VolumeScale.X)
		|| FMath::IsNearlyZero(VolumeScale.Y) || FMath::IsNearlyZero(VolumeScale.Z)
		|| !FMath::IsFinite(VoxelSize) || VoxelSize <= 0.f) return false;
	// Preserve existing axis/voxel convention; only a reviewed dataset may enable it.
	const double LocalVoxelSize = VoxelSize / FMath::Abs(VolumeScale.X);
	if (!FMath::IsFinite(LocalVoxelSize) || LocalVoxelSize <= 0.0) return false;
	const FVector GridPosition = HeterogeneousVolume->GetActorTransform().InverseTransformPosition(WorldLocation) / LocalVoxelSize;
	if (GridPosition.ContainsNaN() || GridPosition.X < 0.0 || GridPosition.X >= DimX
		|| GridPosition.Y < 0.0 || GridPosition.Y >= DimY || GridPosition.Z < 0.0 || GridPosition.Z >= DimZ) return false;
	// Bounds are checked before float->integer conversion; no NaN/overflow indexing.
	const int32 IndexX = FMath::FloorToInt(GridPosition.X);
	const int32 IndexY = FMath::FloorToInt(GridPosition.Y);
	const int32 IndexZ = FMath::FloorToInt(GridPosition.Z);
	const int32 FlatIndex = (IndexX * DimY * DimZ) + (IndexY * DimZ) + IndexZ;
	if (!FramesBuffer[Slot].DensityGrid.IsValidIndex(FlatIndex) || !FramesBuffer[Slot].TemperatureGrid.IsValidIndex(FlatIndex)) return false;
	OutSlot = Slot;
	OutFlatIndex = FlatIndex;
	return true;
}

bool AYUFSBinaryManager::GetSmokeDensityAtLocation(FVector WorldLocation, int32 FrameIndex, uint8& OutDensity)
{
	OutDensity = 0;
	int32 Slot, FlatIndex;
	if (!ResolveLoadedGridIndex(WorldLocation, FrameIndex, Slot, FlatIndex)) return false;
	OutDensity = FramesBuffer[Slot].DensityGrid[FlatIndex];
	return true;
}

bool AYUFSBinaryManager::GetTemperatureAtLocation(FVector WorldLocation, int32 FrameIndex, uint8& OutTemperature)
{
	OutTemperature = 0;
	int32 Slot, FlatIndex;
	if (!ResolveLoadedGridIndex(WorldLocation, FrameIndex, Slot, FlatIndex)) return false;
	OutTemperature = FramesBuffer[Slot].TemperatureGrid[FlatIndex];
	return true;
}
