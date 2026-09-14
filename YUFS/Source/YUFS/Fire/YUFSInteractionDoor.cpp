#include "Fire/YUFSInteractionDoor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Simulation/YUFSSimulationController.h"
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
 PassageBlocker->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
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
FVector AYUFSInteractionDoor::GetHandleLocation() const
{
 // Fixed closed-handle reference: discovery/reach does not jump as the leaf rotates.
 return GetActorTransform().TransformPosition(FVector(0.f,82.f,105.f));
}
bool AYUFSInteractionDoor::IsUserInReach(AActor* User) const
{
 if (!IsValid(User)) return false;
 const FVector Local=GetActorTransform().InverseTransformPosition(User->GetActorLocation());
 return FMath::Abs(Local.X)>=15.f
     && FVector::DistSquared(User->GetActorLocation(),GetHandleLocation())<=FMath::Square(UseDistance);
}
bool AYUFSInteractionDoor::IsPassageClear() const
{
 return IsOpen() && FMath::Abs(OpenAngle)>=75.f && PassageBlocker
     && PassageBlocker->GetCollisionEnabled()==ECollisionEnabled::NoCollision;
}
bool AYUFSInteractionDoor::TryReserve(AActor* User)
{
 if (!IsUserInReach(User) || !CanOperate() || (Operator.IsValid() && Operator.Get()!=User)) return false;
 // A +Y leaf rotated by positive yaw swings toward -X. Choose the opposite
 // side from the first user, then retain it when resuming a partial opening.
 if (OpenFraction <= 0.f)
  SwingDirection = GetActorTransform().InverseTransformPosition(User->GetActorLocation()).X >= 0.f ? 1.f : -1.f;
 Operator=User; return true;
}
bool AYUFSInteractionDoor::TryUse(AActor* User)
{
 if (!TryReserve(User)) return false;
 bOpeningRequested=true;
 return true;
}
void AYUFSInteractionDoor::Release(AActor* User)
{
 if (Operator.Get()!=User) return;
 Operator.Reset(); bOpeningRequested=false; bOpeningBlocked=false;
}
bool AYUFSInteractionDoor::CanSweepLeaf(float FromFraction,float ToFraction) const
{
 if (!GetWorld()) return false;
 auto YawAt=[this](float Fraction)
 {
  return FMath::Abs(OpenAngle)*SwingDirection*Fraction*Fraction*(3.f-2.f*Fraction);
 };
 const float FromYaw=YawAt(FromFraction), ToYaw=YawAt(ToFraction);
 // Rotation-only sweeps do not sweep the arc in UE. Sample the entire arc in
 // <=2 degree steps with a small XY guard, including long/low-frame-rate ticks.
 const int32 Steps=FMath::Max(1,FMath::CeilToInt(FMath::Abs(ToYaw-FromYaw)/2.f));
 FCollisionQueryParams Params(SCENE_QUERY_STAT(DoorLeafSwing),false,this);
 for (int32 Step=1;Step<=Steps;++Step)
 {
  const FQuat LocalRotation(FVector::UpVector,FMath::DegreesToRadians(FMath::Lerp(FromYaw,ToYaw,float(Step)/Steps)));
  const FVector Centre=GetActorTransform().TransformPosition(LocalRotation.RotateVector(FVector(0.f,50.f,110.f)));
  const FQuat Rotation=GetActorQuat()*LocalRotation;
  // A 2cm end/floor tolerance avoids treating authored hinge/trim contact as a
  // person in the swing arc; the actual complete leaf collision remains.
  const FVector Extent=FVector(5.f,48.f,108.f)*GetActorScale3D().GetAbs();
  if (GetWorld()->OverlapBlockingTestByChannel(Centre,Rotation,ECC_Pawn,FCollisionShape::MakeBox(Extent),Params)) return false;
 }
 return true;
}
void AYUFSInteractionDoor::Tick(float Dt)
{
 Super::Tick(Dt);
 if (!SimulationController.IsValid())
  for (TActorIterator<AYUFSSimulationController> It(GetWorld());It;++It) { SimulationController=*It; break; }
 if (SimulationController.IsValid() && !SimulationController->IsNPCSimulationEnabled()) return;
 if (!Operator.IsValid()) { bOpeningRequested=false; bOpeningBlocked=false; return; }
 if (!CanOperate() || !IsUserInReach(Operator.Get())) { Release(Operator.Get()); return; }
 if (!bOpeningRequested || Dt<=0.f) return;
 const float NextFraction=FMath::Min(1.f,OpenFraction+Dt/FMath::Max(.1f,OpenSeconds));
 bOpeningBlocked=!CanSweepLeaf(OpenFraction,NextFraction);
 if (bOpeningBlocked) return;
 OpenFraction=NextFraction;
 // The actor transform stays fixed for discovery, authored placement and route
 // references; only the leaf and its hardware rotate around the hinge pivot.
 const float Eased = OpenFraction*OpenFraction*(3.f-2.f*OpenFraction);
 LeafPivot->SetRelativeRotation(FRotator(0,FMath::Abs(OpenAngle)*SwingDirection*Eased,0));
 if (IsOpen())
 {
  // Only the passage guard disappears. An open solid leaf is still a physical
  // obstacle and never vanishes or lets people walk through its side.
  PassageBlocker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  Release(Operator.Get());
 }
}
