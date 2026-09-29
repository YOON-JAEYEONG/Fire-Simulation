// Fill out your copyright notice in the Description page of Project Settings.

#include "Level/YUFSExitPoint.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float CubeCm = 100.f;       // engine cube edge length
	constexpr float FrameCm = 10.f;       // post / lintel thickness
	const FLinearColor FrameColor(0.05f, 0.35f, 0.12f);
	const FLinearColor LeafColor(0.18f, 0.22f, 0.20f);
	const FLinearColor SignColor(0.10f, 1.00f, 0.25f);
}

AYUFSExitPoint::AYUFSExitPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));

	PostLeft = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PostLeft"));
	PostRight = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PostRight"));
	Lintel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Lintel"));
	DoorLeaf = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DoorLeaf"));
	Sign = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Sign"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	for (UStaticMeshComponent* Part : { PostLeft.Get(), PostRight.Get(), Lintel.Get(), DoorLeaf.Get(), Sign.Get() })
	{
		Part->SetupAttachment(RootComponent);
		if (Cube.Succeeded()) Part->SetStaticMesh(Cube.Object);
		if (ShapeMaterial.Succeeded()) Part->SetMaterial(0, ShapeMaterial.Object);
		// Marker only: never blocks pawns, sight/smoke traces or the navmesh.
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCanEverAffectNavigation(false);
		Part->SetGenerateOverlapEvents(false);
		Part->CastShadow = false;
	}
}

void AYUFSExitPoint::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	const float HalfWidth = FMath::Max(ExitWidth, 60.f) * 0.5f;
	const float FloorZ = -MarkerFloorOffset;
	const float X = MarkerWallDistance - FrameCm * 0.5f; // frame sits against the inner wall face

	auto Place = [](UStaticMeshComponent* Part, const FVector& Center, const FVector& SizeCm, bool bVisible,
		const FLinearColor& Color)
	{
		if (!Part) return;
		Part->SetRelativeLocation(Center);
		Part->SetRelativeScale3D(SizeCm / CubeCm);
		Part->SetVisibility(bVisible);
		if (bVisible)
		{
			if (UMaterialInstanceDynamic* Dynamic = Part->CreateDynamicMaterialInstance(0))
			{
				Dynamic->SetVectorParameterValue(TEXT("Color"), Color);
			}
		}
	};

	const bool bShow = bShowMarker;
	const float PostZ = FloorZ + DoorHeight * 0.5f;
	Place(PostLeft, FVector(X, -HalfWidth - FrameCm * 0.5f, PostZ), FVector(FrameCm, FrameCm, DoorHeight), bShow, FrameColor);
	Place(PostRight, FVector(X, HalfWidth + FrameCm * 0.5f, PostZ), FVector(FrameCm, FrameCm, DoorHeight), bShow, FrameColor);
	Place(Lintel, FVector(X, 0.f, FloorZ + DoorHeight + FrameCm * 0.5f),
		FVector(FrameCm, HalfWidth * 2.f + FrameCm * 2.f, FrameCm), bShow, FrameColor);
	Place(DoorLeaf, FVector(X + FrameCm * 0.25f, 0.f, PostZ), FVector(FrameCm * 0.4f, HalfWidth * 2.f, DoorHeight),
		bShow && bShowDoorLeaf, LeafColor);
	// "EXIT" sign above the lintel, slightly proud of the frame so it reads from the corridor.
	Place(Sign, FVector(X - FrameCm, 0.f, FloorZ + DoorHeight + FrameCm + 18.f), FVector(4.f, 60.f, 22.f), bShow, SignColor);
}
