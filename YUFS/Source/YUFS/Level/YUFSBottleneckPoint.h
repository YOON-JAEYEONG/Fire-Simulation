// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "YUFSBottleneckPoint.generated.h"

UCLASS()
class YUFS_API AYUFSBottleneckPoint : public AActor
{
	GENERATED_BODY()

public:
	AYUFSBottleneckPoint();

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(EditAnywhere, Category="Bottleneck")
	FName BottleneckID;

	UPROPERTY(EditAnywhere, Category="Bottleneck", meta=(ClampMin="80.0"))
	float PassageWidth = 160.f;

	UPROPERTY(EditAnywhere, Category="Bottleneck", meta=(ClampMin="50.0"))
	float DetectionRadiusCm = 180.f;

	UPROPERTY(EditAnywhere, Category="Bottleneck", meta=(ClampMin="50.0"))
	float QueueFrontOffsetCm = 120.f;

	UPROPERTY(EditAnywhere, Category="Bottleneck", meta=(ClampMin="50.0"))
	float QueueRowSpacingCm = 110.f;

	UPROPERTY(EditAnywhere, Category="Bottleneck", meta=(ClampMin="50.0"))
	float LaneSpacingCm = 90.f;

	UPROPERTY(EditAnywhere, Category="Bottleneck", meta=(ClampMin="1"))
	int32 MaxConcurrentLanes = 2;

	UPROPERTY(EditAnywhere, Category="Bottleneck", meta=(ClampMin="50.0"))
	float PassThroughOffsetCm = 140.f;

	UPROPERTY(EditAnywhere, Category="Bottleneck", meta=(ClampMin="0.0"))
	float ReleaseForwardDistanceCm = 100.f;
};
