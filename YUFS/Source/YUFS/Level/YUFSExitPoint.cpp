// Fill out your copyright notice in the Description page of Project Settings.

#include "Level/YUFSExitPoint.h"
#include "Components/SceneComponent.h"

AYUFSExitPoint::AYUFSExitPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
}
