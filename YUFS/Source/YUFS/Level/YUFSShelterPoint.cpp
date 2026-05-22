// Fill out your copyright notice in the Description page of Project Settings.


#include "Level/YUFSShelterPoint.h"
#include "Components/SceneComponent.h"

// Sets default values
AYUFSShelterPoint::AYUFSShelterPoint()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
}

// Called when the game starts or when spawned
void AYUFSShelterPoint::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AYUFSShelterPoint::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

