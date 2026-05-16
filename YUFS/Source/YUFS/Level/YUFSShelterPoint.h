// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "YUFSShelterPoint.generated.h"

UCLASS()
class YUFS_API AYUFSShelterPoint : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AYUFSShelterPoint();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	
public:
	UPROPERTY(EditAnywhere, Category="Shelter")
	int32 MaxCapacity = 20;         // 수용 가능 인원

	UPROPERTY(EditAnywhere, Category="Shelter")
	bool bHasVentilation = true;    // 환기 여부 (연기 차단 효과)

	int32 CurrentOccupancy = 0;     // 런타임에 갱신
	bool IsFull() const { return CurrentOccupancy >= MaxCapacity; }

};
