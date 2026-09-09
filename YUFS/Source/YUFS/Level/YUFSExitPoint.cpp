// Fill out your copyright notice in the Description page of Project Settings.

#include "Level/YUFSExitPoint.h"
#include "Components/SceneComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Level/YUFSLevelDataManager.h"

AYUFSExitPoint::AYUFSExitPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
}

void AYUFSExitPoint::BeginPlay()
{
	Super::BeginPlay();

	RegisteredLevelDataManager = Cast<AYUFSLevelDataManager>(
		UGameplayStatics::GetActorOfClass(GetWorld(), AYUFSLevelDataManager::StaticClass()));
	if (RegisteredLevelDataManager)
	{
		RegisteredLevelDataManager->RegisterExitPoint(this);
	}
}

void AYUFSExitPoint::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (RegisteredLevelDataManager)
	{
		RegisteredLevelDataManager->UnregisterExitPoint(this);
		RegisteredLevelDataManager = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}
