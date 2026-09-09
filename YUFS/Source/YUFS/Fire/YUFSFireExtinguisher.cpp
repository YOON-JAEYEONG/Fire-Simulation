#include "Fire/YUFSFireExtinguisher.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AYUFSFireExtinguisher::AYUFSFireExtinguisher()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderAsset(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeAsset(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ImportedMainAsset(TEXT(
		"/Game/Props/FireExtinguisher/OpenGameArt_JamesWhite_2015/"
		"SM_FireExtinguisher_OpenGameArt.SM_FireExtinguisher_OpenGameArt"));

	bUsingImportedVisual = ImportedMainAsset.Succeeded();

	ImportedMainMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ImportedMain"));
	ImportedMainMesh->SetupAttachment(SceneRoot);
	ImportedMainMesh->SetStaticMesh(ImportedMainAsset.Object);
	// The OBJ is Y-up and authored at one tenth of the desired real-world scale.
	ImportedMainMesh->SetRelativeRotation(FRotator(0.f, 0.f, 90.f));
	ImportedMainMesh->SetRelativeScale3D(FVector(10.f));
	if (bUsingImportedVisual)
	{
		const FBox Box = ImportedMainAsset.Object->GetBoundingBox().TransformBy(
			FTransform(FRotator(0.f, 0.f, 90.f), FVector::ZeroVector, FVector(10.f)));
		ImportedMainMesh->SetRelativeLocation(FVector(0, 0, -Box.Min.Z));
	}
	// Interchange's generic PBR material is not flagged for Nanite. Force the
	// fallback mesh so its supplied diffuse texture renders instead of the dark
	// default material in-game.
	ImportedMainMesh->bDisallowNanite = true;
	ImportedMainMesh->SetVisibility(bUsingImportedVisual);
	ImportedMainMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	ImportedMainMesh->SetCollisionEnabled(
		bUsingImportedVisual ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);

	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	BodyMesh->SetupAttachment(SceneRoot);
	BodyMesh->SetStaticMesh(CylinderAsset.Object);
	BodyMesh->SetRelativeLocation(FVector(0.f, 0.f, 46.f));
	BodyMesh->SetRelativeScale3D(FVector(0.19f, 0.19f, 0.46f));
	BodyMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	BodyMesh->SetVisibility(!bUsingImportedVisual);
	BodyMesh->SetCollisionEnabled(
		bUsingImportedVisual ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);

	TopMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Top"));
	TopMesh->SetupAttachment(SceneRoot);
	TopMesh->SetStaticMesh(CylinderAsset.Object);
	TopMesh->SetRelativeLocation(FVector(0.f, 0.f, 95.f));
	TopMesh->SetRelativeScale3D(FVector(0.15f, 0.15f, 0.06f));
	TopMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TopMesh->SetVisibility(!bUsingImportedVisual);

	HandleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Handle"));
	HandleMesh->SetupAttachment(SceneRoot);
	HandleMesh->SetStaticMesh(CubeAsset.Object);
	HandleMesh->SetRelativeLocation(FVector(14.f, 0.f, 108.f));
	HandleMesh->SetRelativeScale3D(FVector(0.30f, 0.055f, 0.045f));
	HandleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HandleMesh->SetVisibility(!bUsingImportedVisual);

	HoseMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Hose"));
	HoseMesh->SetupAttachment(SceneRoot);
	HoseMesh->SetStaticMesh(CylinderAsset.Object);
	HoseMesh->SetRelativeLocation(FVector(34.f, 0.f, 91.f));
	HoseMesh->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));
	HoseMesh->SetRelativeScale3D(FVector(0.025f, 0.025f, 0.35f));
	HoseMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HoseMesh->SetVisibility(!bUsingImportedVisual);

	NozzlePoint = CreateDefaultSubobject<USceneComponent>(TEXT("NozzlePoint"));
	NozzlePoint->SetupAttachment(SceneRoot);
	// Imported CC0 model: the horn projects along local +Y after the Y-up correction.
	NozzlePoint->SetRelativeLocation(
		bUsingImportedVisual ? FVector(0.f, 43.f, 49.f) : FVector(69.f, 0.f, 91.f));

	SprayStreamMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SprayStream"));
	SprayStreamMesh->SetupAttachment(SceneRoot);
	SprayStreamMesh->SetStaticMesh(CylinderAsset.Object);
	SprayStreamMesh->SetRelativeLocation(
		bUsingImportedVisual ? FVector(112.f, 0.f, 46.f) : FVector(245.f, 0.f, 91.f));
	SprayStreamMesh->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));
	SprayStreamMesh->SetRelativeScale3D(
		bUsingImportedVisual ? FVector(0.025f, 0.025f, 1.f) : FVector(0.035f, 0.035f, 2.1f));
	SprayStreamMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SprayStreamMesh->SetVisibility(false);
	SprayStreamMesh->SetCastShadow(false);

	StatusText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("StatusText"));
	StatusText->SetupAttachment(SceneRoot);
	StatusText->SetRelativeLocation(bUsingImportedVisual ? FVector(0.f, 0.f, 76.f) : FVector(0.f, 0.f, 145.f));
	StatusText->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));
	StatusText->SetHorizontalAlignment(EHTA_Center);
	StatusText->SetWorldSize(24.f);
	StatusText->SetTextRenderColor(FColor::White);
	StatusText->SetText(FText::FromString(TEXT("EXTINGUISHER\nAVAILABLE")));
}

void AYUFSFireExtinguisher::BeginPlay()
{
	Super::BeginPlay();
	RemainingAgent = FMath::Max(0.f, AgentCapacity);
	RefreshVisualState();
	UE_LOG(
		LogTemp,
		Display,
		TEXT("[YUFSExtinguisher] Visual=%s, RemainingAgent=%.2f"),
		bUsingImportedVisual ? TEXT("OpenGameArt CC0 Fire Extinguisher by JamesWhite") : TEXT("Engine primitive fallback"),
		RemainingAgent);
	if (bUsingImportedVisual && ImportedMainMesh)
	{
		const FVector VisualSize = ImportedMainMesh->Bounds.BoxExtent * 2.f;
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[YUFSExtinguisher] Imported visual bounds cm=(%.1f, %.1f, %.1f)"),
			VisualSize.X,
			VisualSize.Y,
			VisualSize.Z);
	}
}

bool AYUFSFireExtinguisher::TryReserve(AActor* Requester)
{
	if (!IsValid(Requester) || RemainingAgent <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	if (ReservationOwner.IsValid() && ReservationOwner.Get() != Requester)
	{
		return false;
	}
	ReservationOwner = Requester;
	if (State == EYUFSFireExtinguisherState::Available)
	{
		State = EYUFSFireExtinguisherState::Reserved;
	}
	RefreshVisualState();
	return true;
}

bool AYUFSFireExtinguisher::PickUp(AActor* Requester, USceneComponent* AttachParent, FName SocketName)
{
	if (!TryReserve(Requester) || !IsValid(AttachParent))
	{
		return false;
	}

	SetPrimaryCollisionEnabled(ECollisionEnabled::NoCollision);
	AttachToComponent(AttachParent, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName);
	SetActorRelativeLocation(FVector(0.f, 0.f, -40.f));
	SetActorRelativeRotation(FRotator::ZeroRotator);
	SetActorScale3D(FVector(0.85f));
	State = EYUFSFireExtinguisherState::Held;
	RefreshVisualState();
	return true;
}

bool AYUFSFireExtinguisher::StartSpraying(AActor* Requester)
{
	if (ReservationOwner.Get() != Requester || RemainingAgent <= KINDA_SMALL_NUMBER)
	{
		return false;
	}
	State = EYUFSFireExtinguisherState::Spraying;
	RefreshVisualState();
	return true;
}

void AYUFSFireExtinguisher::StopSpraying(AActor* Requester)
{
	if (ReservationOwner.Get() == Requester && State == EYUFSFireExtinguisherState::Spraying)
	{
		State = RemainingAgent > KINDA_SMALL_NUMBER
			? EYUFSFireExtinguisherState::Held
			: EYUFSFireExtinguisherState::Empty;
		RefreshVisualState();
	}
}

float AYUFSFireExtinguisher::ConsumeAgent(AActor* Requester, float RequestedAmount)
{
	if (ReservationOwner.Get() != Requester || State != EYUFSFireExtinguisherState::Spraying)
	{
		return 0.f;
	}

	const float Consumed = FMath::Min(FMath::Max(0.f, RequestedAmount), RemainingAgent);
	RemainingAgent -= Consumed;
	if (RemainingAgent <= KINDA_SMALL_NUMBER)
	{
		RemainingAgent = 0.f;
		State = EYUFSFireExtinguisherState::Empty;
		RefreshVisualState();
	}
	return Consumed;
}

void AYUFSFireExtinguisher::Release(AActor* Requester)
{
	if (ReservationOwner.Get() != Requester)
	{
		return;
	}
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	ReservationOwner.Reset();
	State = RemainingAgent > KINDA_SMALL_NUMBER
		? EYUFSFireExtinguisherState::Available
		: EYUFSFireExtinguisherState::Empty;
	SetPrimaryCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	RefreshVisualState();
}

void AYUFSFireExtinguisher::SetPrimaryCollisionEnabled(ECollisionEnabled::Type CollisionEnabled)
{
	if (bUsingImportedVisual && ImportedMainMesh)
	{
		ImportedMainMesh->SetCollisionEnabled(CollisionEnabled);
	}
	else if (BodyMesh)
	{
		BodyMesh->SetCollisionEnabled(CollisionEnabled);
	}
}

float AYUFSFireExtinguisher::GetRemainingAgentNormalized() const
{
	return AgentCapacity > KINDA_SMALL_NUMBER
		? FMath::Clamp(RemainingAgent / AgentCapacity, 0.f, 1.f)
		: 0.f;
}

FVector AYUFSFireExtinguisher::GetNozzleWorldLocation() const
{
	return NozzlePoint ? NozzlePoint->GetComponentLocation() : GetActorLocation();
}

void AYUFSFireExtinguisher::ShowSprayToward(const FVector& Target)
{
	if (State != EYUFSFireExtinguisherState::Spraying) return;
	const FVector Start = GetNozzleWorldLocation();
	const FVector Direction = Target - Start;
	SprayStreamMesh->SetWorldLocation((Start + Target) * 0.5f);
	SprayStreamMesh->SetWorldRotation(FRotationMatrix::MakeFromZ(Direction).Rotator());
	SprayStreamMesh->SetWorldScale3D(FVector(0.05f, 0.05f, Direction.Size() / 100.f));
	SprayStreamMesh->SetVisibility(true);
}

void AYUFSFireExtinguisher::RefreshVisualState()
{
	if (!ImportedVisualMaterial && bUsingImportedVisual && ImportedMainMesh)
	{
		ImportedVisualMaterial = ImportedMainMesh->CreateAndSetMaterialInstanceDynamic(0);
	}
	if (!BodyMaterial && BodyMesh)
	{
		BodyMaterial = BodyMesh->CreateAndSetMaterialInstanceDynamic(0);
	}
	if (!SprayMaterial && SprayStreamMesh)
	{
		SprayMaterial = SprayStreamMesh->CreateAndSetMaterialInstanceDynamic(0);
	}

	if (BodyMaterial)
	{
		const FLinearColor Color = State == EYUFSFireExtinguisherState::Empty
			? FLinearColor(0.08f, 0.08f, 0.08f)
			: FLinearColor(0.85f, 0.015f, 0.01f);
		BodyMaterial->SetVectorParameterValue(TEXT("Color"), Color);
	}
	if (ImportedVisualMaterial)
	{
		// Preserve the downloaded diffuse texture while adding a small red fill
		// contribution. The source model has hard normals and otherwise reads as
		// a black silhouette on the camera-facing side of the building demo.
		ImportedVisualMaterial->SetVectorParameterValue(
			TEXT("EmissiveColor"),
			State == EYUFSFireExtinguisherState::Empty
				? FLinearColor(0.02f, 0.02f, 0.02f)
				: FLinearColor(0.16f, 0.006f, 0.004f));
		ImportedVisualMaterial->SetScalarParameterValue(TEXT("EmissiveColorMapWeight"), 0.f);
	}
	if (SprayMaterial)
	{
		SprayMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.75f, 0.92f, 1.f));
	}

	// The interaction/motion adapter owns the world-space spray visualization so
	// there is exactly one beam and it starts at the real nozzle position.
	SprayStreamMesh->SetVisibility(false);
	if (StatusText)
	{
		StatusText->SetVisibility(
			State == EYUFSFireExtinguisherState::Available
			|| State == EYUFSFireExtinguisherState::Reserved);
		FString Label;
		switch (State)
		{
		case EYUFSFireExtinguisherState::Available: Label = TEXT("EXTINGUISHER\nAVAILABLE"); break;
		case EYUFSFireExtinguisherState::Reserved: Label = TEXT("EXTINGUISHER\nRESERVED"); break;
		case EYUFSFireExtinguisherState::Held: Label = TEXT("EXTINGUISHER\nHELD"); break;
		case EYUFSFireExtinguisherState::Spraying: Label = TEXT("EXTINGUISHER\nSPRAYING"); break;
		case EYUFSFireExtinguisherState::Empty: Label = TEXT("EXTINGUISHER\nEMPTY"); break;
		default: break;
		}
		StatusText->SetText(FText::FromString(Label));
	}
}
