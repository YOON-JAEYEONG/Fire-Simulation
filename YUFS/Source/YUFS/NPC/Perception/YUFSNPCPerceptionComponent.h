// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "YUFSPerceptionConfig.h"
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
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

public:
	void UpdatePerception(int32 CurrentFrame);
	float SampleSmokeAtPoint(FVector WorldPos, int32 Frame) const;

	float GetSmokeDensity() const { return CachedSmokeDensity; }
	float GetTemperature() const { return CachedTemperature; }
	float GetSmokeInFrontNormalized() const { return CachedSmokeInFrontNormalized; }
	float GetSmokeAboveNormalized() const { return CachedSmokeAboveNormalized; }
	float GetRiskLevel() const { return CachedRiskLevel; }
	bool IsIncapacitated() const { return CachedSmokeDensity > Config->IncapacitationThreshold; }

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

	float ComputeRiskLevel(float Density, float Temp) const
	{
		const float D = FMath::Pow(Density, 2.0f);
		const float T = FMath::Clamp(Temp, 0.f, 1.f);
		return FMath::Clamp(D * 0.7f + T * 0.3f, 0.f, 1.f);
	}
};
