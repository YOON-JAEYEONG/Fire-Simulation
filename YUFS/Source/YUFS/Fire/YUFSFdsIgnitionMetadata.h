#pragma once

#include "CoreMinimal.h"

/**
 * Explicit, externally reviewed FDS ignition metadata. This supplies a world location,
 * not NPC awareness, risk, combustion state or a writable suppression model.
 * No BIN/SVT bounds, actor transform or axis convention is inferred here.
 */
class YUFS_API FYUFSFdsIgnitionMetadata
{
public:
	/** Pure parser. A failed parse resets OutMetadata, including any old valid target. */
	static bool ParseJson(const FString& Json, FYUFSFdsIgnitionMetadata& OutMetadata, FString& OutReason);
	/** Reads the sidecar and calls the same parser. Does not parse or alter FDS/BIN/SVT. */
	static bool LoadFile(const FString& Path, FYUFSFdsIgnitionMetadata& OutMetadata, FString& OutReason);

	/** Input is SimulationController.GetElapsedTime(), never volume frame / playback FPS. */
	bool TryGetActiveIgnition(double SimulationTimeSeconds, FVector& OutWorldLocation, FString& OutReason) const;
	bool IsValid() const { return bValid; }
	const FString& GetSelectedIgnitionId() const { return SelectedIgnitionId; }
	const FString& GetSourceFile() const { return SourceFile; }
	const FString& GetSourceSha256() const { return SourceSha256; }

private:
	bool bValid = false;
	FString SourceFile;
	FString SourceSha256;
	FString SelectedIgnitionId;
	FVector WorldLocation = FVector::ZeroVector;
	double FdsTimeAtSimulationZeroSeconds = 0.0;
	double FdsSecondsPerSimulationSecond = 0.0;
	double ActivationTimeSeconds = 0.0;
};
