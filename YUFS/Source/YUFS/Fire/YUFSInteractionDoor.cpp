#include "Fire/YUFSInteractionDoor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
AYUFSInteractionDoor::AYUFSInteractionDoor()
{
 PrimaryActorTick.bCanEverTick = true;
 RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Hinge"));
 LeafPivot = CreateDefaultSubobject<USceneComponent>(TEXT("LeafPivot"));
 LeafPivot->SetupAttachment(RootComponent);
 Panel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Panel"));
 Panel->SetupAttachment(LeafPivot);
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
 Panel->SetStaticMesh(Cube.Object);
 Panel->SetRelativeLocation(FVector(0, 50, 110));
 Panel->SetRelativeScale3D(FVector(.06, 1, 2.2));
 Panel->SetCollisionProfileName(TEXT("BlockAll"));
 // Route can reach the door; physical collision blocks passage until fully open.
 // Locked doors must also be represented by the route team's blocked-edge model.
 Panel->SetCanEverAffectNavigation(false);

 // Keep the entire doorway blocked until the opening action completes, even
 // when the animated leaf has already rotated out of a pawn's centre line.
 PassageBlocker = CreateDefaultSubobject<UBoxComponent>(TEXT("PassageBlocker"));
 PassageBlocker->SetupAttachment(RootComponent);
 PassageBlocker->SetRelativeLocation(FVector(0, 50, 110));
 PassageBlocker->SetBoxExtent(FVector(5, 50, 110));
 PassageBlocker->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
 PassageBlocker->SetCollisionResponseToAllChannels(ECR_Ignore);
 PassageBlocker->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
 PassageBlocker->SetCanEverAffectNavigation(false);

 auto AddTrim = [this](FName Name, USceneComponent* Parent, FVector Position, FVector Scale)
 {
  auto* Mesh = CreateDefaultSubobject<UStaticMeshComponent>(Name);
  Mesh->SetupAttachment(Parent);
  Mesh->SetStaticMesh(Cube.Object);
  Mesh->SetRelativeLocation(Position);
  Mesh->SetRelativeScale3D(Scale);
  Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  Mesh->SetCanEverAffectNavigation(false);
  return Mesh;
 };
 FrameMeshes.Add(AddTrim(TEXT("HingeJamb"), RootComponent, FVector(0,-3,110), FVector(.12,.06,2.2)));
 FrameMeshes.Add(AddTrim(TEXT("LatchJamb"), RootComponent, FVector(0,103,110), FVector(.12,.06,2.2)));
 FrameMeshes.Add(AddTrim(TEXT("Lintel"), RootComponent, FVector(0,50,223), FVector(.12,1.12,.06)));
 // Both faces have a backplate, spindle and lever, so either approach reads as
 // a usable door. The hardware is attached to the unscaled hinge, not the panel.
 for (int32 Side : {-1, 1})
 {
  const FString Suffix = Side < 0 ? TEXT("Back") : TEXT("Front");
  HardwareMeshes.Add(AddTrim(FName(*(TEXT("HandlePlate")+Suffix)), LeafPivot,
      FVector(Side*3.8f,82,105), FVector(.015,.07,.19)));
  HardwareMeshes.Add(AddTrim(FName(*(TEXT("HandleSpindle")+Suffix)), LeafPivot,
      FVector(Side*6.f,82,105), FVector(.06,.035,.035)));
  HardwareMeshes.Add(AddTrim(FName(*(TEXT("HandleLever")+Suffix)), LeafPivot,
      FVector(Side*9.f,75,105), FVector(.035,.18,.035)));
 }
 for (int32 Index=0; Index<3; ++Index)
  HardwareMeshes.Add(AddTrim(FName(*FString::Printf(TEXT("VisibleHinge%d"),Index)), LeafPivot,
      FVector(4,0,35+Index*75), FVector(.06,.055,.16)));
}
void AYUFSInteractionDoor::OnConstruction(const FTransform& Transform)
{
 Super::OnConstruction(Transform);
 // Reuse engine-owned primitive material. No external textures or mesh assets.
 auto* Base = LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
 if (!Base) return;
 auto MakeFinish = [this, Base](const FLinearColor& Color, float Roughness)
 {
  auto* Material = UMaterialInstanceDynamic::Create(Base,this);
  Material->SetVectorParameterValue(TEXT("Color"),Color);
  Material->SetScalarParameterValue(TEXT("Roughness"),Roughness);
  return Material;
 };
 Panel->SetMaterial(0,MakeFinish(FLinearColor(.035f,.20f,.34f),.48f));
 auto* FrameFinish = MakeFinish(FLinearColor(.085f,.10f,.12f),.6f);
 for (const auto& Mesh : FrameMeshes) Mesh->SetMaterial(0,FrameFinish);
 auto* HardwareFinish = MakeFinish(FLinearColor(.65f,.69f,.73f),.15f);
 for (const auto& Mesh : HardwareMeshes) Mesh->SetMaterial(0,HardwareFinish);
}
bool AYUFSInteractionDoor::TryUse(AActor* User)
{
 if (!IsValid(User) || !CanOperate() || (Operator.IsValid() && Operator.Get()!=User)
     || FVector::DistSquared(User->GetActorLocation(), GetActorLocation()+FVector(0,0,90)) > FMath::Square(UseDistance)) return false;
 // A +Y leaf rotated by positive yaw swings toward -X. Choose the opposite
 // side from the first user, then retain it when resuming a partial opening.
 if (OpenFraction <= 0.f)
  SwingDirection = GetActorTransform().InverseTransformPosition(User->GetActorLocation()).X >= 0.f ? 1.f : -1.f;
 Operator=User; return true;
}
void AYUFSInteractionDoor::Release(AActor* User) { if (Operator.Get()==User) Operator.Reset(); }
void AYUFSInteractionDoor::Tick(float Dt)
{
 Super::Tick(Dt);
 if (!Operator.IsValid() || !CanOperate()) return;
 if (FVector::DistSquared(Operator->GetActorLocation(), GetActorLocation()+FVector(0,0,90)) > FMath::Square(UseDistance)) { Operator.Reset(); return; }
 OpenFraction=FMath::Min(1.f,OpenFraction+Dt/FMath::Max(.1f,OpenSeconds));
 // The actor transform stays fixed for discovery, authored placement and route
 // references; only the leaf and its hardware rotate around the hinge pivot.
 const float Eased = OpenFraction*OpenFraction*(3.f-2.f*OpenFraction);
 LeafPivot->SetRelativeRotation(FRotator(0,FMath::Abs(OpenAngle)*SwingDirection*Eased,0));
 if (IsOpen())
 {
  Panel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  PassageBlocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  Operator.Reset();
 }
}
