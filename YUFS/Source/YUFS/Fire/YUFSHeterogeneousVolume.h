// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/HeterogeneousVolumeComponent.h"
#include "GameFramework/Actor.h"
#include "YUFSHeterogeneousVolume.generated.h"

UCLASS()
class YUFS_API AYUFSHeterogeneousVolume : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AYUFSHeterogeneousVolume();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	
protected:
	UPROPERTY(EditAnywhere)
	UHeterogeneousVolumeComponent* HeterogeneousVolumeComponent;
	
public:
	int32 GetFrame() const;

};
