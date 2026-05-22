
// Fill out your copyright notice in the Description page of Project Settings.

#include "Level/YUFSBottleneckPoint.h"

#include "Components/SceneComponent.h"

AYUFSBottleneckPoint::AYUFSBottleneckPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
}

void AYUFSBottleneckPoint::BeginPlay()
{
	Super::BeginPlay();
}

void AYUFSBottleneckPoint::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}
