// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "YUFSNPCDebugComponent.generated.h"

class AYUFSEvacuationNPC;
struct FYUFSNPCObservation;

UCLASS(ClassGroup=(YUFS), meta=(BlueprintSpawnableComponent))
class YUFS_API UYUFSNPCDebugComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UYUFSNPCDebugComponent();

protected:
	virtual void BeginPlay() override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, Category="Debug")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, Category="Debug")
	float MaxDrawDistance = 3000.f;

	UPROPERTY(EditAnywhere, Category="Debug")
	bool bOnlyDrawWhenRecentlyRendered = true;

	UPROPERTY(EditAnywhere, Category="Debug")
	float RecentRenderToleranceSeconds = 0.3f;

	UPROPERTY(EditAnywhere, Category="Debug")
	float TextHeightOffset = 110.f;

	UPROPERTY(EditAnywhere, Category="Debug")
	float ObservationTextHeightOffset = 210.f;

	UPROPERTY(EditAnywhere, Category="Debug")
	float RiskRingRadius = 55.f;

	UPROPERTY(EditAnywhere, Category="Debug")
	float PathHeightOffset = 12.f;

	UPROPERTY(EditAnywhere, Category="Debug")
	bool bShowState = true;

	UPROPERTY(EditAnywhere, Category="Debug")
	bool bShowRiskLevel = true;

	UPROPERTY(EditAnywhere, Category="Debug")
	bool bShowPath = false;

	UPROPERTY(EditAnywhere, Category="Debug")
	bool bShowObservation = false;

	void DrawDebugOverlay(const FYUFSNPCObservation& Obs);

private:
	bool ShouldDraw() const;
	FColor GetRiskColor(float NormalizedRisk) const;
	FString BuildStateText(const FYUFSNPCObservation& Obs) const;
	FString BuildObservationText(const FYUFSNPCObservation& Obs) const;

	TWeakObjectPtr<AYUFSEvacuationNPC> OwnerNPC;
};
