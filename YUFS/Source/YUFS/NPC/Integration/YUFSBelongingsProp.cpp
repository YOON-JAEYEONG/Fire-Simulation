#include "NPC/Integration/YUFSBelongingsProp.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AYUFSBelongingsProp::AYUFSBelongingsProp()
{
	PrimaryActorTick.bCanEverTick=false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("FloorPivot")));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Rounded(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Flat(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Sphere=Rounded.Object; Cube=Flat.Object;
	for (int32 I=0;I<8;++I)
	{
		auto* Part=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Visual%d"),I));
		Part->SetupAttachment(GetRootComponent());
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetGenerateOverlapEvents(false);
		Part->SetCanEverAffectNavigation(false);
		Parts.Add(Part);
	}
}

void AYUFSBelongingsProp::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshVisuals();
}

void AYUFSBelongingsProp::BeginPlay()
{
	Super::BeginPlay();
	RefreshVisuals();
}

void AYUFSBelongingsProp::RefreshVisuals()
{
	auto Material=[](const TCHAR* Name)
	{
		return LoadObject<UMaterialInterface>(nullptr,*FString::Printf(TEXT("/Game/Props/Belongings/%s.%s"),Name,Name),nullptr,LOAD_NoWarn);
	};
	if (!FabricMaterial) FabricMaterial=Material(TEXT("MI_BagOcean"));
	auto* Dark=Material(TEXT("MI_Trim"));
	auto* Light=Material(TEXT("MI_Label"));
	auto* Accent=Material(TEXT("MI_Zip"));
	auto* Paper=Material(TEXT("MI_Paper"));
	auto Part=[&](int32 Index,UStaticMesh* Mesh,FVector Position,FVector Size,UMaterialInterface* Color,FRotator Turn=FRotator::ZeroRotator)
	{
		auto* Visual=Parts[Index].Get();
		Visual->SetStaticMesh(Mesh);
		Visual->SetVisibility(Mesh!=nullptr);
		Visual->SetRelativeTransform(FTransform(Turn,Position,Size/100.0));
		if (Mesh && Color) Visual->SetMaterial(0,Color);
	};
	if (Kind==EYUFSBelongingsPropKind::Notebook)
	{
		Part(0,Cube,FVector(0,0,1.8),FVector(29,21,2.0),Paper);
		Part(1,Cube,FVector(0,0,3.0),FVector(30,22,.7),FabricMaterial);
		Part(2,Cube,FVector(0,0,.45),FVector(30,22,.7),FabricMaterial);
		Part(3,Cube,FVector(0,-10.8,1.8),FVector(30,.9,3.2),Dark);
		Part(4,Cube,FVector(-5,1,3.42),FVector(10,7,.16),Light);
		for (int32 I=5;I<Parts.Num();++I) Part(I,nullptr,FVector::ZeroVector,FVector::OneVector,Dark);
		return;
	}
	// Local +X is the front (pocket). Pivot is the bottom contact on the floor.
	Part(0,Sphere,FVector(0,0,21.5),FVector(25,34,43),FabricMaterial);
	Part(1,Sphere,FVector(10,0,12),FVector(17,28,23),FabricMaterial);
	Part(2,Cube,FVector(16,0,17),FVector(1.8,19,1.1),Dark);
	Part(3,Cube,FVector(16,-7,15.6),FVector(2.0,1.3,3),Accent);
	Part(4,Sphere,FVector(-9,-10,19),FVector(5,5,35),Dark,FRotator(8,0,-9));
	Part(5,Sphere,FVector(-9,10,19),FVector(5,5,35),Dark,FRotator(8,0,9));
	Part(6,Cube,FVector(0,0,43.5),FVector(4,12,2.8),Dark);
	Part(7,Sphere,FVector(-1,0,40.0),FVector(6,15,6),Dark);
}

void AYUFSBelongingsProp::PlaceOnFloor(FVector Floor,float Yaw)
{
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	SetActorLocationAndRotation(Floor,FRotator(0,Yaw,0));
	SetActorScale3D(FVector::OneVector);
	SetActorHiddenInGame(false);
}

void AYUFSBelongingsProp::CarryOn(USceneComponent& Parent,float BodyHalfHeight)
{
	AttachToComponent(&Parent,FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	SetActorRelativeTransform(FTransform(FRotator(0,180,0),FVector(-34,0,BodyHalfHeight*.1),FVector(.8)));
	SetActorHiddenInGame(false);
}
