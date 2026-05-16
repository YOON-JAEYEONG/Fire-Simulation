// Fill out your copyright notice in the Description page of Project Settings.


#include "YUFSHeterogeneousVolume.h"

// Sets default values
AYUFSHeterogeneousVolume::AYUFSHeterogeneousVolume()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	
	HeterogeneousVolumeComponent = CreateDefaultSubobject<UHeterogeneousVolumeComponent>(TEXT("YUFSHeterogeneousVolumeComponent"));
	HeterogeneousVolumeComponent->EndFrame = 0.0f;
	HeterogeneousVolumeComponent->bPlaying = false;
}

// Called when the game starts or when spawned
void AYUFSHeterogeneousVolume::BeginPlay()
{
	Super::BeginPlay();
	
	HeterogeneousVolumeComponent->Frame = 0.0f;
	HeterogeneousVolumeComponent->FrameRate = 8.0f;
	HeterogeneousVolumeComponent->EndFrame = 8000;
	HeterogeneousVolumeComponent->bPlaying = true;
	
}

// Called every frame
void AYUFSHeterogeneousVolume::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

int32 AYUFSHeterogeneousVolume::GetFrame() const
{
	if (HeterogeneousVolumeComponent)
	{
		return static_cast<int32>(HeterogeneousVolumeComponent->Frame);
	}
	UE_LOG(LogTemp, Error, TEXT("HeterogeneousVolumeComponent가 비어있습니다!"));
	return 0;
}

