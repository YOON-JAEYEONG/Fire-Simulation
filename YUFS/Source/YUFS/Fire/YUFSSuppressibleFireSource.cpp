#include "Fire/YUFSSuppressibleFireSource.h"

#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AYUFSSuppressibleFireSource::AYUFSSuppressibleFireSource()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereAsset(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderAsset(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	FuelBase = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FuelBase"));
	FuelBase->SetupAttachment(SceneRoot);
	FuelBase->SetStaticMesh(CylinderAsset.Object);
	FuelBase->SetRelativeLocation(FVector(0.f, 0.f, 10.f));
	FuelBase->SetRelativeScale3D(FVector(0.65f, 0.65f, 0.10f));
	FuelBase->SetCollisionProfileName(TEXT("BlockAllDynamic"));

	FlameOuter = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FlameOuter"));
	FlameOuter->SetupAttachment(SceneRoot);
	FlameOuter->SetStaticMesh(SphereAsset.Object);
	FlameOuter->SetRelativeLocation(FVector(0.f, 0.f, 80.f));
	FlameOuter->SetRelativeScale3D(FVector(0.60f, 0.60f, 1.25f));
	FlameOuter->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FlameOuter->SetCastShadow(false);

	FlameInner = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FlameInner"));
	FlameInner->SetupAttachment(SceneRoot);
	FlameInner->SetStaticMesh(SphereAsset.Object);
	FlameInner->SetRelativeLocation(FVector(0.f, 0.f, 62.f));
	FlameInner->SetRelativeScale3D(FVector(0.34f, 0.34f, 0.75f));
	FlameInner->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FlameInner->SetCastShadow(false);

	FireLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FireLight"));
	FireLight->SetupAttachment(SceneRoot);
	FireLight->SetRelativeLocation(FVector(0.f, 0.f, 105.f));
	FireLight->SetLightColor(FLinearColor(1.f, 0.12f, 0.01f));
	FireLight->SetIntensity(6000.f);
	FireLight->SetAttenuationRadius(900.f);
	FireLight->SetCastShadows(false);

	StatusText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("StatusText"));
	StatusText->SetupAttachment(SceneRoot);
	StatusText->SetRelativeLocation(FVector(0.f, 0.f, 210.f));
	StatusText->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));
	StatusText->SetHorizontalAlignment(EHTA_Center);
	StatusText->SetWorldSize(28.f);
	StatusText->SetTextRenderColor(FColor(255, 190, 40));
	StatusText->SetText(FText::FromString(TEXT("INITIAL FIRE\n100%")));
}

void AYUFSSuppressibleFireSource::BeginPlay()
{
	Super::BeginPlay();
	OuterMaterial = FlameOuter->CreateAndSetMaterialInstanceDynamic(0);
	InnerMaterial = FlameInner->CreateAndSetMaterialInstanceDynamic(0);
	if (OuterMaterial)
	{
		OuterMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.025f, 0.005f));
	}
	if (InnerMaterial)
	{
		InnerMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.72f, 0.02f));
	}
	RefreshVisualState();
}

void AYUFSSuppressibleFireSource::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (IsExtinguished())
	{
		return;
	}
	VisualTime += DeltaTime;
	const float FlickerA = 0.92f + FMath::Sin(VisualTime * 7.1f) * 0.08f;
	const float FlickerB = 0.90f + FMath::Sin(VisualTime * 9.7f + 1.3f) * 0.10f;
	const float Size = FMath::Lerp(0.15f, 1.f, IntensityNormalized);
	FlameOuter->SetRelativeScale3D(FVector(0.60f * FlickerA, 0.60f * FlickerB, 1.25f * Size));
	FlameInner->SetRelativeScale3D(FVector(0.34f * FlickerB, 0.34f * FlickerA, 0.75f * Size));
	FireLight->SetIntensity(6000.f * IntensityNormalized * FlickerA);
}

float AYUFSSuppressibleFireSource::ApplySuppression(float Amount)
{
	const float Applied = FMath::Min(FMath::Max(0.f, Amount), IntensityNormalized);
	IntensityNormalized -= Applied;
	if (IntensityNormalized <= KINDA_SMALL_NUMBER)
	{
		IntensityNormalized = 0.f;
	}
	RefreshVisualState();
	return Applied;
}

void AYUFSSuppressibleFireSource::RefreshVisualState()
{
	const bool bBurning = !IsExtinguished();
	FlameOuter->SetVisibility(bBurning);
	FlameInner->SetVisibility(bBurning);
	FireLight->SetVisibility(bBurning);
	StatusText->SetText(FText::FromString(bBurning
		? FString::Printf(TEXT("INITIAL FIRE\n%03.0f%%"), IntensityNormalized * 100.f)
		: TEXT("FIRE EXTINGUISHED\nSAFE")));
	StatusText->SetTextRenderColor(bBurning ? FColor(255, 190, 40) : FColor(70, 255, 110));
}
