#include "Fire/YUFSBinaryManager.h"
#include "Fire/YUFSHeterogeneousVolume.h"
#include "Components/HeterogeneousVolumeComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Async/Async.h"
#include "DrawDebugHelpers.h"
#include "Materials/MaterialInterface.h"
#include "SparseVolumeTexture/SparseVolumeTexture.h"

AYUFSBinaryManager::AYUFSBinaryManager()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
}

void AYUFSBinaryManager::BeginPlay()
{
	Super::BeginPlay();
	if (!IsValid(HeterogeneousVolume))
		HeterogeneousVolume = Cast<AYUFSHeterogeneousVolume>(UGameplayStatics::GetActorOfClass(GetWorld(), AYUFSHeterogeneousVolume::StaticClass()));
	const FString FullPath = FPaths::ProjectContentDir() / BinaryFilePath;
	TUniquePtr<IFileHandle> File(FPlatformFileManager::Get().GetPlatformFile().OpenRead(*FullPath));
	if (!File)
	{
		UE_LOG(LogTemp, Error, TEXT("[YUFS][Hazard] Missing binary: %s"), *FullPath);
		return;
	}
	int32 Header[4] = {};
	if (!File->Read(reinterpret_cast<uint8*>(Header), sizeof(Header)) ||
		Header[0] <= 0 || Header[0] > 1000000 ||
		Header[1] <= 0 || Header[1] > 4096 || Header[2] <= 0 || Header[2] > 4096 || Header[3] <= 0 || Header[3] > 4096)
	{
		UE_LOG(LogTemp, Error, TEXT("[YUFS][Hazard] Invalid binary header: %s"), *FullPath);
		return;
	}
	const int64 GridSize = static_cast<int64>(Header[1]) * Header[2] * Header[3];
	if (GridSize > 4 * 1024 * 1024 || File->Size() != 16 + 2 * GridSize * Header[0])
	{
		UE_LOG(LogTemp, Error, TEXT("[YUFS][Hazard] Binary dimensions/length invalid: %s"), *FullPath);
		return;
	}
	TotalFrames = Header[0]; DimX = Header[1]; DimY = Header[2]; DimZ = Header[3];
	bHeaderValid = true;
	FramesBuffer.SetNum(MaxBufferSize);
	LoadedFrameIndices.Init(INDEX_NONE, MaxBufferSize);
	UE_LOG(LogTemp, Log, TEXT("Parsed Binary Header -> Frames: %d, DimX: %d, DimY: %d, DimZ: %d"), TotalFrames, DimX, DimY, DimZ);
	UE_LOG(LogTemp, Warning, TEXT("[YUFS][Hazard] %s"), *GetHazardDiagnostics());
	if (bDrawVoxelDebug)
		GetWorld()->GetTimerManager().SetTimer(DebugTimerHandle, this, &AYUFSBinaryManager::PlayDebugAnimation, 0.1f, true);
}

void AYUFSBinaryManager::EndPlay(const EEndPlayReason::Type Reason)
{
	++LoadGeneration;
	bHeaderValid = false;
	bIsLoadingChunk = false;
	GetWorld()->GetTimerManager().ClearTimer(DebugTimerHandle);
	FramesBuffer.Reset();
	LoadedFrameIndices.Reset();
	Super::EndPlay(Reason);
}

void AYUFSBinaryManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bHeaderValid || !IsValid(HeterogeneousVolume)) return;
	if (!FMath::IsFinite(BinaryFramesPerVisualFrame) || BinaryFramesPerVisualFrame <= 0)
	{
		CurrentDebugFrame = INDEX_NONE;
		return;
	}
	const double MappedFrame = static_cast<double>(HeterogeneousVolume->GetFrame()) * BinaryFramesPerVisualFrame + BinaryFrameOffset;
	CurrentDebugFrame = MappedFrame >= 0 && MappedFrame < TotalFrames ? FMath::FloorToInt(MappedFrame) : INDEX_NONE;
	if (FMath::Abs(CurrentDebugFrame - LastCurrentFrame) > 50)
	{
		++LoadGeneration;
		bIsLoadingChunk = false;
	}
	LastCurrentFrame = CurrentDebugFrame;
	if (CurrentDebugFrame < 0 || bIsLoadingChunk) return;
	const int32 Start = FMath::Max(0, CurrentDebugFrame - 24);
	const int32 End = FMath::Min(TotalFrames, CurrentDebugFrame + 120);
	// Load the requested frame first after a seek, then look behind/ahead.
	if (LoadedFrameIndices[CurrentDebugFrame % MaxBufferSize] != CurrentDebugFrame)
	{
		LoadDynamicChunkAsync(CurrentDebugFrame, FMath::Min(End, CurrentDebugFrame + FMath::Clamp(ChunkSize, 1, 120)), LoadGeneration);
		return;
	}
	for (int32 Frame = Start; Frame < End; ++Frame)
	{
		if (LoadedFrameIndices[Frame % MaxBufferSize] != Frame)
		{
			LoadDynamicChunkAsync(Frame, FMath::Min(End, Frame + FMath::Clamp(ChunkSize, 1, 120)), LoadGeneration);
			break;
		}
	}
}

void AYUFSBinaryManager::SetHeterogeneousVolume(AYUFSHeterogeneousVolume* InVolume)
{
	HeterogeneousVolume = InVolume;
}

void AYUFSBinaryManager::LoadDynamicChunkAsync(int32 Start, int32 End, int32 Generation)
{
	bIsLoadingChunk = true;
	const FString FullPath = FPaths::ProjectContentDir() / BinaryFilePath;
	const FIntVector Dimensions(DimX, DimY, DimZ);
	const TWeakObjectPtr<AYUFSBinaryManager> WeakThis(this);
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [WeakThis, FullPath, Dimensions, Start, End, Generation]()
	{
		TArray<TSharedPtr<const FYUFSHazardGrid, ESPMode::ThreadSafe>> Loaded;
		TUniquePtr<IFileHandle> File(FPlatformFileManager::Get().GetPlatformFile().OpenRead(*FullPath));
		const int32 GridSize = Dimensions.X * Dimensions.Y * Dimensions.Z;
		bool bOK = File && File->Seek(16 + static_cast<int64>(Start) * GridSize * 2);
		for (int32 Frame = Start; bOK && Frame < End; ++Frame)
		{
			auto Grid = MakeShared<FYUFSHazardGrid, ESPMode::ThreadSafe>();
			Grid->Dimensions = Dimensions;
			Grid->Density.SetNumUninitialized(GridSize);
			Grid->Temperature.SetNumUninitialized(GridSize);
			bOK = File->Read(Grid->Density.GetData(), GridSize) && File->Read(Grid->Temperature.GetData(), GridSize);
			Loaded.Add(Grid);
		}
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Start, Generation, bOK, Loaded = MoveTemp(Loaded)]()
		{
			AYUFSBinaryManager* Self = WeakThis.Get();
			if (!Self || !Self->bHeaderValid || Generation != Self->LoadGeneration) return;
			if (bOK)
			{
				for (int32 I = 0; I < Loaded.Num(); ++I)
				{
					const int32 Frame = Start + I;
					Self->FramesBuffer[Frame % Self->MaxBufferSize] = Loaded[I];
					Self->LoadedFrameIndices[Frame % Self->MaxBufferSize] = Frame;
				}
			}
			else
			{
				Self->bHeaderValid = false;
				UE_LOG(LogTemp, Error, TEXT("[YUFS][Hazard] Binary frame read failed; navigation cannot classify this data as safe."));
			}
			Self->bIsLoadingChunk = false;
		});
	});
}

FYUFSHazardSnapshot AYUFSBinaryManager::GetHazardSnapshot(int32 Frame) const
{
	FYUFSHazardSnapshot Result;
	Result.Frame = Frame;
	if (!bHeaderValid) return Result;
	if (!IsValid(HeterogeneousVolume) || !FMath::IsFinite(VoxelSize) || VoxelSize <= 0)
	{
		Result.Status = EYUFSHazardDataStatus::InvalidMapping;
		return Result;
	}
	const auto* Volume = HeterogeneousVolume->FindComponentByClass<UHeterogeneousVolumeComponent>();
	const FTransform VolumeTransform = Volume ? Volume->GetComponentTransform() : HeterogeneousVolume->GetActorTransform();
	const double ScaleX = FMath::Abs(VolumeTransform.GetScale3D().X);
	if (!VolumeTransform.IsValid() || ScaleX <= SMALL_NUMBER)
	{
		Result.Status = EYUFSHazardDataStatus::InvalidMapping;
		return Result;
	}
	// Compatibility with IT: VoxelSize is a world-space X spacing; Y/Z follow their own transform scales.
	Result.GridToWorld = FTransform(FQuat::Identity, GridOriginLocal, FVector(VoxelSize / ScaleX)) * VolumeTransform;
	if (!Result.GridToWorld.IsValid() || Result.GridToWorld.GetScale3D().GetAbsMin() <= SMALL_NUMBER)
	{
		Result.Status = EYUFSHazardDataStatus::InvalidMapping;
		return Result;
	}
	if (Frame < 0 || Frame >= TotalFrames)
	{
		Result.Status = EYUFSHazardDataStatus::InvalidFrame;
		return Result;
	}
	const int32 Slot = Frame % MaxBufferSize;
	if (!LoadedFrameIndices.IsValidIndex(Slot) || LoadedFrameIndices[Slot] != Frame || !FramesBuffer[Slot].IsValid())
	{
		Result.Status = EYUFSHazardDataStatus::Loading;
		return Result;
	}
	Result.Grid = FramesBuffer[Slot];
	Result.Status = EYUFSHazardDataStatus::Ready;
	return Result;
}

bool AYUFSBinaryManager::GetSmokeDensityAtLocation(FVector Location, int32 Frame, uint8& Density)
{
	const auto Sample = GetHazardSnapshot(Frame).Sample(Location);
	Density = static_cast<uint8>(FMath::RoundToInt(Sample.Smoke * 255));
	return Sample.Status == EYUFSHazardDataStatus::Ready;
}

bool AYUFSBinaryManager::GetTemperatureAtLocation(FVector Location, int32 Frame, uint8& Temperature)
{
	const auto Sample = GetHazardSnapshot(Frame).Sample(Location);
	Temperature = static_cast<uint8>(FMath::RoundToInt(Sample.Heat * 255));
	return Sample.Status == EYUFSHazardDataStatus::Ready;
}

FString AYUFSBinaryManager::GetHazardDiagnostics() const
{
	const auto* Volume = IsValid(HeterogeneousVolume) ? HeterogeneousVolume->FindComponentByClass<UHeterogeneousVolumeComponent>() : nullptr;
	FString VisualAsset = TEXT("None");
	if (Volume && Volume->GetMaterial(0))
	{
		TArray<FMaterialParameterInfo> Parameters;
		TArray<FGuid> Ids;
		Volume->GetMaterial(0)->GetAllSparseVolumeTextureParameterInfo(Parameters, Ids);
		for (const auto& Parameter : Parameters)
		{
			USparseVolumeTexture* Texture = nullptr;
			if (Volume->GetMaterial(0)->GetSparseVolumeTextureParameterValue(Parameter, Texture) && Texture)
			{
				VisualAsset = FString::Printf(TEXT("%s frames=%d importTransform=%s"), *Texture->GetPathName(),
					Texture->GetNumFrames(), *Texture->GetFrameTransform().ToString());
				break;
			}
		}
	}
	return FString::Printf(TEXT("file=%s binary=%dx%dx%d/%d frame=%d visual=%s/%d voxelXcm=%.2f alignment=%s status=%s asset=%s"),
		*BinaryFilePath, DimX, DimY, DimZ, TotalFrames, CurrentDebugFrame,
		Volume ? *Volume->VolumeResolution.ToString() : TEXT("MissingVolume"),
		HeterogeneousVolume ? HeterogeneousVolume->GetFrame() : INDEX_NONE, VoxelSize,
		bDatasetAlignmentConfirmed ? TEXT("UserConfirmed") : TEXT("UNVERIFIED"),
		*StaticEnum<EYUFSHazardDataStatus>()->GetNameStringByValue(static_cast<int64>(GetDataStatus())), *VisualAsset);
}

void AYUFSBinaryManager::PlayDebugAnimation()
{
	const auto Snapshot = GetHazardSnapshot(CurrentDebugFrame);
	if (Snapshot.Status != EYUFSHazardDataStatus::Ready) return;
	const int32 Step = FMath::Max(4, DebugStep);
	const FVector Extent = Snapshot.GridToWorld.GetScale3D().GetAbs() * 0.45f;
	for (int32 X = 0; X < DimX; X += Step)
	for (int32 Y = 0; Y < DimY; Y += Step)
	for (int32 Z = 0; Z < DimZ; Z += Step)
	{
		const FVector Center = Snapshot.GridToWorld.TransformPosition(FVector(X + 0.5, Y + 0.5, Z + 0.5));
		const auto Sample = Snapshot.Sample(Center);
		if (Sample.Smoke * 255 >= DensityThreshold || Sample.Heat * 255 >= TemperatureThreshold)
			DrawDebugBox(GetWorld(), Center, Extent, Snapshot.GridToWorld.GetRotation(),
				Sample.Heat * 255 >= TemperatureThreshold ? TemperatureColor : DensityColor, false, 0.11f);
	}
}
