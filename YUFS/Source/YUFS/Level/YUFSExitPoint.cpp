// Fill out your copyright notice in the Description page of Project Settings.


#include "Level/YUFSExitPoint.h"
#include "Components/SceneComponent.h"

// Sets default values
AYUFSExitPoint::AYUFSExitPoint()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

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

