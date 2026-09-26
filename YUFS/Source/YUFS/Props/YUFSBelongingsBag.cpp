#include "Props/YUFSBelongingsBag.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Engine cube is 100 cm per side; the bag is roughly a 55 x 30 x 40 cm duffel/backpack.
	const FVector BodyScale(0.55f, 0.30f, 0.40f);
	const FVector StrapScale(0.30f, 0.06f, 0.08f);
	constexpr float BodyHalfHeightCm = 20.f;
}

AYUFSBelongingsBag::AYUFSBelongingsBag()
{
	PrimaryActorTick.bCanEverTick = false;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Strap = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Strap"));
	Strap->SetupAttachment(Body);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	for (UStaticMeshComponent* Part : { Body.Get(), Strap.Get() })
	{
		if (Cube.Succeeded()) Part->SetStaticMesh(Cube.Object);
		if (ShapeMaterial.Succeeded()) Part->SetMaterial(0, ShapeMaterial.Object);
		// Visual target only: never an obstacle for pawns, sight traces or the navmesh.
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCanEverAffectNavigation(false);
		Part->SetGenerateOverlapEvents(false);
		Part->CastShadow = true;
	}
	Body->SetRelativeScale3D(BodyScale);
	// Strap loop on top of the bag, expressed in the body's (scaled) local space.
	Strap->SetRelativeScale3D(StrapScale / BodyScale);
	Strap->SetRelativeLocation(FVector(0.f, 0.f, 50.f + StrapScale.Z * 50.f / BodyScale.Z));
}

void AYUFSBelongingsBag::BeginPlay()
{
	Super::BeginPlay();
	for (UStaticMeshComponent* Part : { Body.Get(), Strap.Get() })
	{
		if (UMaterialInstanceDynamic* Dynamic = Part ? Part->CreateDynamicMaterialInstance(0) : nullptr)
		{
			Dynamic->SetVectorParameterValue(TEXT("Color"), Part == Strap.Get() ? BagColor * 0.4f : BagColor);
		}
	}
}

void AYUFSBelongingsBag::AssignOwnerNpc(AActor* InOwner)
{
	OwnerNpc = InOwner;
}

FVector AYUFSBelongingsBag::GetPickupLocation() const
{
	return GetActorLocation() - FVector(0.f, 0.f, BodyHalfHeightCm);
}

bool AYUFSBelongingsBag::AttachToCarrier(AActor* Carrier)
{
	if (!IsValid(Carrier) || IsCarried() || (OwnerNpc.IsValid() && OwnerNpc.Get() != Carrier)
		|| !Carrier->GetRootComponent()) return false;
	State = EYUFSBelongingsState::Carried;
	AttachToComponent(Carrier->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
	SetActorRelativeLocation(CarryOffset);
	// Upright on the back, strap towards the shoulders.
	SetActorRelativeRotation(FRotator(0.f, 90.f, 0.f));
	return true;
}
