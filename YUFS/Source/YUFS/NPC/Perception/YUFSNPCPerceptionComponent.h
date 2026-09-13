// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "YUFSPerceptionConfig.h"
#include "Fire/YUFSHazardField.h"
#include "Components/ActorComponent.h"
#include "YUFSNPCPerceptionComponent.generated.h"

class AYUFSBinaryManager;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class YUFS_API UYUFSNPCPerceptionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UYUFSNPCPerceptionComponent();

protected:
	virtual void BeginPlay() override;

public:
	void UpdatePerception(int32 CurrentFrame);
	// Also used by synthetic perception tests; tracing still uses the actual world collision.
	void UpdateFromSnapshot(const FYUFSHazardSnapshot& Snapshot, float Now);
	FYUFSHazardSnapshot RestrictToKnowledge(FYUFSHazardSnapshot Snapshot) const;
	void ReceiveHazardReport(const UYUFSNPCPerceptionComponent& Other);
	float GetHeatInSight() const { return CachedHeatInSight; }
	float GetNearbyHeat() const { return CachedNearbyHeat; }
	int32 GetKnownCellCount() const { return KnownCells.Num(); }
	EYUFSHazardDataStatus GetDataStatus() const { return DataStatus; }
	UPROPERTY(EditAnywhere, Category="Config") float HazardMemorySeconds = 30.f;
	float SampleSmokeAtPoint(FVector WorldPos, int32 Frame) const;

	float GetSmokeDensity() const { return CachedSmokeDensity; }
	float GetTemperature() const { return CachedTemperature; }
	float GetSmokeInFrontNormalized() const { return CachedSmokeInFrontNormalized; }
	float GetSmokeAboveNormalized() const { return CachedSmokeAboveNormalized; }
	float GetRiskLevel() const { return CachedRiskLevel; }
	bool IsIncapacitated() const { return Config && CachedSmokeDensity > Config->IncapacitationThreshold; }

	UPROPERTY(EditAnywhere, Category="Config")
	UYUFSPerceptionConfig* Config;

private:
	UPROPERTY()
	AYUFSBinaryManager* BinaryManager;
	float CachedSmokeDensity = 0.f;
	float CachedTemperature = 0.f;
	float CachedSmokeInFrontNormalized = 0.f;
	float CachedSmokeAboveNormalized = 0.f;
	float CachedRiskLevel = 0.f;
	float CachedHeatInSight = 0.f;
	float CachedNearbyHeat = 0.f;
	EYUFSHazardDataStatus DataStatus = EYUFSHazardDataStatus::MissingData;
	TMap<int32, FYUFSHazardSample> KnownCells;
	TMap<int32, float> LastObservedAt;
	TSharedPtr<const TMap<int32, FYUFSHazardSample>, ESPMode::ThreadSafe> PublishedKnowledge;
	FTransform LastGridTransform = FTransform::Identity;
	FIntVector LastGridDimensions = FIntVector::ZeroValue;
	int32 LastFrame = INDEX_NONE;

	float ComputeRiskLevel(float Density, float Temp) const
	{
		const float D = FMath::Pow(Density, 2.0f);
		const float T = FMath::Clamp(Temp, 0.f, 1.f);
		return FMath::Clamp(D * 0.7f + T * 0.3f, 0.f, 1.f);
	}
};
