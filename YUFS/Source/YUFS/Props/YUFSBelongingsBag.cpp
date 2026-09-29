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
		// Static mesh components default to Static mobility, and the engine refuses to attach a
		// Static component to a moving NPC at runtime -- the bag would stay on the floor.
		Part->SetMobility(EComponentMobility::Movable);
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

bool AYUFSBelongingsBag::Claim(AActor* InOwner)
{
	if (!IsValid(InOwner) || IsClaimed() || IsCarried()) return false;
	// Captured lazily: NPCs may claim before this bag's own BeginPlay has run.
	if (!bHomeCaptured) { HomeTransform = GetActorTransform(); bHomeCaptured = true; }
	OwnerNpc = InOwner;
	return true;
}

void AYUFSBelongingsBag::ReleaseToHome()
{
	if (GetAttachParentActor()) DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	if (bHomeCaptured) SetActorTransform(HomeTransform, false, nullptr, ETeleportType::TeleportPhysics);
	SetActorHiddenInGame(false);
	State = EYUFSBelongingsState::LeftBehind;
	OwnerNpc.Reset();
}

void AYUFSBelongingsBag::LeaveWithCarrier()
{
	if (GetAttachParentActor()) DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	SetActorHiddenInGame(true);
	// Stays Carried so no other NPC can claim a bag that has already left the building.
	OwnerNpc.Reset();
}

FVector AYUFSBelongingsBag::GetPickupLocation() const
{
	return GetActorLocation() - FVector(0.f, 0.f, BodyHalfHeightCm);
}

bool AYUFSBelongingsBag::AttachToCarrier(AActor* Carrier)
{
	if (!IsValid(Carrier) || IsCarried() || (OwnerNpc.IsValid() && OwnerNpc.Get() != Carrier)
		|| !Carrier->GetRootComponent()) return false;
	// Bags saved in a map before the mobility fix may still carry Static; lift them first.
	for (UStaticMeshComponent* Part : { Body.Get(), Strap.Get() })
	{
		if (Part && Part->Mobility != EComponentMobility::Movable) Part->SetMobility(EComponentMobility::Movable);
	}
	if (!AttachToComponent(Carrier->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform))
	{
		UE_LOG(LogTemp, Warning, TEXT("[NPCBelongings] %s could not attach to %s"), *GetName(), *Carrier->GetName());
		return false;
	}
	State = EYUFSBelongingsState::Carried;
	SetActorRelativeLocation(CarryOffset);
	// Upright on the back, strap towards the shoulders.
	SetActorRelativeRotation(FRotator(0.f, 90.f, 0.f));
	UE_LOG(LogTemp, Display, TEXT("[NPCBelongings] %s on %s's back (%.0f cm from its centre, parent=%s)"),
		*GetName(), *Carrier->GetName(), FVector::Dist(GetActorLocation(), Carrier->GetActorLocation()),
		GetAttachParentActor() ? *GetAttachParentActor()->GetName() : TEXT("none"));
	return true;
}
