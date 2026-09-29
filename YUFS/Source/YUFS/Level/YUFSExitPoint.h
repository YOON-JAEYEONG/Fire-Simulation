// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "YUFSExitPoint.generated.h"

class UStaticMeshComponent;

UCLASS()
class YUFS_API AYUFSExitPoint : public AActor
{
	GENERATED_BODY()
	
public:
	AYUFSExitPoint();

	virtual void OnConstruction(const FTransform& Transform) override;

public:
	// 에디터에서 각 출구마다 설정
	UPROPERTY(EditAnywhere, Category="Exit")
	FName ExitID;           // "MainEntrance", "EmergencyExit_B2" 등

	UPROPERTY(EditAnywhere, Category="Exit")
	float ExitWidth = 150.f; // 출구 폭 (혼잡도 계산용)

	UPROPERTY(EditAnywhere, Category="Exit")
	bool bIsFamiliarEntry = false; // NPC 진입 시 사용한 출구인지

	// --- Visual marker (door frame + green EXIT sign). Purely cosmetic: no collision, no navmesh effect.
	// The actor's forward (+X) must point OUT of the building; the frame is drawn MarkerWallDistance ahead.

	UPROPERTY(EditAnywhere, Category="Exit|Marker")
	bool bShowMarker = true;

	// Also draw a closed door leaf. Use it where the building mesh has no opening at this exit.
	UPROPERTY(EditAnywhere, Category="Exit|Marker")
	bool bShowDoorLeaf = false;

	// Distance from this actor to the inner face of the wall, along the actor's forward axis (cm).
	UPROPERTY(EditAnywhere, Category="Exit|Marker", meta=(ClampMin="0"))
	float MarkerWallDistance = 60.f;

	// Height of this actor above the floor (cm); the frame starts at the floor.
	UPROPERTY(EditAnywhere, Category="Exit|Marker", meta=(ClampMin="0"))
	float MarkerFloorOffset = 90.f;

	UPROPERTY(EditAnywhere, Category="Exit|Marker", meta=(ClampMin="100"))
	float DoorHeight = 210.f;

private:
	UPROPERTY(VisibleAnywhere, Category="Exit|Marker")
	TObjectPtr<UStaticMeshComponent> PostLeft;

	UPROPERTY(VisibleAnywhere, Category="Exit|Marker")
	TObjectPtr<UStaticMeshComponent> PostRight;

	UPROPERTY(VisibleAnywhere, Category="Exit|Marker")
	TObjectPtr<UStaticMeshComponent> Lintel;

	UPROPERTY(VisibleAnywhere, Category="Exit|Marker")
	TObjectPtr<UStaticMeshComponent> DoorLeaf;

	UPROPERTY(VisibleAnywhere, Category="Exit|Marker")
	TObjectPtr<UStaticMeshComponent> Sign;
};
