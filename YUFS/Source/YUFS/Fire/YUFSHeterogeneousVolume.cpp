#include "Fire/YUFSHeterogeneousVolume.h"
#include "Simulation/YUFSSimulationController.h"
#include "EngineUtils.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

AYUFSHeterogeneousVolume::AYUFSHeterogeneousVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	HeterogeneousVolumeComponent = CreateDefaultSubobject<UHeterogeneousVolumeComponent>(TEXT("YUFSHeterogeneousVolumeComponent"));
	HeterogeneousVolumeComponent->EndFrame = 0.f;
	HeterogeneousVolumeComponent->bPlaying = false;
}
void AYUFSHeterogeneousVolume::BeginPlay()
{
	Super::BeginPlay();
	if (IgnitionMetadataFile.IsEmpty())
		GConfig->GetString(TEXT("YUFS.FdsIgnition"), TEXT("MetadataFile"), IgnitionMetadataFile, GGameIni);
	if (!IgnitionMetadataFile.IsEmpty())
	{
		const FString File = FPaths::IsRelative(IgnitionMetadataFile)
			? FPaths::Combine(FPaths::ProjectContentDir(), IgnitionMetadataFile) : IgnitionMetadataFile;
		bMetadataLoaded = FYUFSFdsIgnitionMetadata::LoadFile(File, IgnitionMetadata, MetadataDiagnostic);
	}
	else MetadataDiagnostic = TEXT("MissingFdsIgnitionMetadata");
	UE_LOG(LogTemp, Display, TEXT("[FDSIgnition] %s metadata=%d status=%s. No local fire effects or suppression response."),
		*GetName(), bMetadataLoaded, *MetadataDiagnostic);
	if (HeterogeneousVolumeComponent)
	{
		HeterogeneousVolumeComponent->Frame = 0.f;
		HeterogeneousVolumeComponent->FrameRate = PlaybackFrameRate;
		HeterogeneousVolumeComponent->EndFrame = TotalFrameCount;
		HeterogeneousVolumeComponent->bPlaying = bAutoPlayOnBeginPlay;
	}
	bFireActive = bAutoPlayOnBeginPlay;
}
void AYUFSHeterogeneousVolume::StartFire()
{
	bFireActive = true;
	if (!HeterogeneousVolumeComponent) return;
	HeterogeneousVolumeComponent->Frame = 0.f;
	HeterogeneousVolumeComponent->bPlaying = true;
	UE_LOG(LogTemp, Display, TEXT("[YUFSFire] Recorded FDS playback started; NPC attempts do not extinguish the data."));
}
void AYUFSHeterogeneousVolume::PauseFire()
{
	if (HeterogeneousVolumeComponent) HeterogeneousVolumeComponent->bPlaying = false;
}
void AYUFSHeterogeneousVolume::ResumeFire()
{
	if (HeterogeneousVolumeComponent) HeterogeneousVolumeComponent->bPlaying = true;
}
void AYUFSHeterogeneousVolume::ResetFire()
{
	bFireActive = false;
	if (!HeterogeneousVolumeComponent) return;
	HeterogeneousVolumeComponent->Frame = 0.f;
	HeterogeneousVolumeComponent->bPlaying = false;
}
int32 AYUFSHeterogeneousVolume::GetFrame() const
{
	return HeterogeneousVolumeComponent ? static_cast<int32>(HeterogeneousVolumeComponent->Frame) : 0;
}
void AYUFSHeterogeneousVolume::SetFrame(int32 TargetFrame)
{
	if (!HeterogeneousVolumeComponent) return;
	const int32 MaxFrame = FMath::Max(0, FMath::FloorToInt(TotalFrameCount) - 1);
	HeterogeneousVolumeComponent->Frame = static_cast<float>(FMath::Clamp(TargetFrame, 0, MaxFrame));
	HeterogeneousVolumeComponent->EndFrame = TotalFrameCount;
	HeterogeneousVolumeComponent->bPlaying = false;
}
bool AYUFSHeterogeneousVolume::IsPlaying() const
{
	return HeterogeneousVolumeComponent && HeterogeneousVolumeComponent->bPlaying;
}
bool AYUFSHeterogeneousVolume::IsFdsFireActive() const
{
	FVector Location;
	return bFireActive && GetInteractionTarget(Location);
}
bool AYUFSHeterogeneousVolume::GetInteractionTarget(FVector& OutWorldLocation) const
{
	OutWorldLocation = FVector::ZeroVector;
	if (!bMetadataLoaded || !GetWorld()) return false;
	for (TActorIterator<AYUFSSimulationController> It(GetWorld()); It; ++It)
	{
		FString Reason;
		// Explicit simulation-clock mapping, never an assumed SVT fps / BIN time ratio.
		return IgnitionMetadata.TryGetActiveIgnition(It->GetElapsedTime(), OutWorldLocation, Reason);
	}
	return false;
}
