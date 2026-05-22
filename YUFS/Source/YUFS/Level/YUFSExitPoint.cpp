// Fill out your copyright notice in the Description page of Project Settings.


#include "Level/YUFSExitPoint.h"
#include "Components/SceneComponent.h"

// Sets default values
AYUFSExitPoint::AYUFSExitPoint()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
}

// Called when the game starts or when spawned
void AYUFSExitPoint::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AYUFSExitPoint::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

