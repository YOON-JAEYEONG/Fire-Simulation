#include "Fire/YUFSInteractionDoor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "EngineUtils.h"
#include "Simulation/YUFSSimulationController.h"
#include "Simulation/YUFSTimelineTypes.h"
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
 return IsValid(User) && IsLocationInReach(User->GetActorLocation());
}
bool AYUFSInteractionDoor::IsLocationInReach(FVector Location) const
{
 const FVector Local=GetActorTransform().InverseTransformPosition(Location);
 return FMath::Abs(Local.X)>=15.f
     && FVector::DistSquared(Location,GetHandleLocation())<=FMath::Square(UseDistance);
}
bool AYUFSInteractionDoor::IsPassageClear() const
{
 return IsOpen() && FMath::Abs(OpenAngle)>=75.f && PassageBlocker
     && PassageBlocker->GetCollisionEnabled()==ECollisionEnabled::NoCollision;
}
FTransform AYUFSInteractionDoor::GetSwingTransform(float Yaw) const
{
 // Match the leaf pivot's attachment so a wide door stays wide as it rotates.
 return FTransform(FRotator(0,Yaw,0)) * GetActorTransform();
}
double AYUFSInteractionDoor::GetMaximumSwingRadius() const
{
 const FVector Scale=GetActorScale3D().GetAbs();
 return 100.0*FMath::Max(Scale.X,Scale.Y);
}
bool AYUFSInteractionDoor::IsInsidePropClearance(FVector FloorLocation,float ExtraClearanceCm) const
{
 const FVector Hinge=GetActorLocation();
 // Before a user reserves the door it can swing toward either side.
 return FMath::Abs(FloorLocation.Z-Hinge.Z)<=80.0
     && FVector::DistSquared2D(Hinge,FloorLocation)<=FMath::Square(GetMaximumSwingRadius()+FMath::Max(0.f,ExtraClearanceCm));
}
bool AYUFSInteractionDoor::ConflictsWithOpening(const ACharacter& Walker, FVector From, FVector To) const
{
 if (bTimelinePresentation || !CanOperate() || !Operator.IsValid() || Operator.Get()==&Walker) return false;
 return IntersectsRemainingSwing(Walker,From,To);
}
bool AYUFSInteractionDoor::IsInOpeningSwing(const ACharacter& Character, FVector Location) const
{
 return CanOperate() && IntersectsRemainingSwing(Character,Location,Location);
}
bool AYUFSInteractionDoor::IntersectsRemainingSwing(const ACharacter& Character, FVector From, FVector To) const
{
 const auto* Capsule=Character.GetCapsuleComponent();
 const FVector Hinge=GetActorLocation();
 const FVector Scale=GetActorScale3D().GetAbs();
 const double HalfHeight=Capsule->GetScaledCapsuleHalfHeight();
 if (FMath::Min(From.Z,To.Z)-HalfHeight>Hinge.Z+220.f*Scale.Z
     || FMath::Max(From.Z,To.Z)+HalfHeight<Hinge.Z) return false;
 const double Clearance=Capsule->GetScaledCapsuleRadius()+3.f*Scale.X+8.f;
 if (FVector::Dist2D(Hinge,From)>GetMaximumSwingRadius()+Clearance+FVector::Dist2D(From,To)) return false;

 From.Z=To.Z=0.f;
 const FVector FlatHinge(Hinge.X,Hinge.Y,0.f);
 const float FinalYaw=FMath::Abs(OpenAngle)*SwingDirection;
 const float CurrentYaw=FinalYaw*OpenFraction*OpenFraction*(3.f-2.f*OpenFraction);
 const int32 Steps=FMath::Max(1,FMath::CeilToInt(FMath::Abs(FinalYaw-CurrentYaw)/5.f));
 for (int32 Step=0;Step<=Steps;++Step)
 {
  const float Yaw=FMath::Lerp(CurrentYaw,FinalYaw,float(Step)/Steps);
  FVector LeafEnd=GetSwingTransform(Yaw).TransformPosition(FVector(0,100,0));
  LeafEnd.Z=0.f;
  FVector OnRoute,OnLeaf;
  FMath::SegmentDistToSegmentSafe(From,To,FlatHinge,LeafEnd,OnRoute,OnLeaf);
  if (FVector::DistSquared(OnRoute,OnLeaf)<=FMath::Square(Clearance)) return true;
 }
 return false;
}

bool AYUFSInteractionDoor::TryReserve(AActor* User)
{
 if (bTimelinePresentation || !IsUserInReach(User) || !CanOperate() || (Operator.IsValid() && Operator.Get()!=User)) return false;
 // A +Y leaf rotated by positive yaw swings toward -X.
 // Retain the chosen direction when resuming a partial opening.
 if (OpenFraction <= 0.f)
 {
  switch (SwingSide)
  {
   case EYUFSDoorSwingSide::LocalNegativeX: SwingDirection=1.f; break;
   case EYUFSDoorSwingSide::LocalPositiveX: SwingDirection=-1.f; break;
   default:
    SwingDirection=GetActorTransform().InverseTransformPosition(User->GetActorLocation()).X>=0.f ? 1.f : -1.f;
    break;
  }
 }
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
bool AYUFSInteractionDoor::CanSweepLeaf(float FromFraction,float ToFraction,bool bReportBlocker) const
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
  const FTransform Swing=GetSwingTransform(FMath::Lerp(FromYaw,ToYaw,float(Step)/Steps));
  const FVector Centre=Swing.TransformPosition(FVector(0.f,50.f,110.f));
  // A 2cm end/floor tolerance avoids treating authored hinge/trim contact as a
  // person in the swing arc; the actual complete leaf collision remains.
  const FVector Extent=FVector(5.f,48.f,108.f)*Swing.GetScale3D().GetAbs();
  const FCollisionShape Leaf=FCollisionShape::MakeBox(Extent);
  if (!GetWorld()->OverlapBlockingTestByChannel(Centre,Swing.GetRotation(),ECC_Pawn,Leaf,Params)) continue;
  if (bReportBlocker)
  {
   TArray<FOverlapResult> Hits;
   GetWorld()->OverlapMultiByChannel(Hits,Centre,Swing.GetRotation(),ECC_Pawn,Leaf,Params);
   for (const FOverlapResult& Hit : Hits)
   {
    if (!Hit.bBlockingHit) continue;
    UE_LOG(LogTemp,Log,TEXT("[YUFS][Door] door=%s operator=%s fraction=%.3f blocker=%s component=%s"),
        *GetName(),*GetNameSafe(Operator.Get()),FromFraction,*GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()));
   }
  }
  return false;
 }
 return true;
}
void AYUFSInteractionDoor::Tick(float Dt)
{
 Super::Tick(Dt);
 if (bTimelinePresentation) return;
 if (!SimulationController.IsValid())
  for (TActorIterator<AYUFSSimulationController> It(GetWorld());It;++It) { SimulationController=*It; break; }
 if (SimulationController.IsValid() && !SimulationController->IsNPCSimulationEnabled()) return;
 if (!Operator.IsValid()) { bOpeningRequested=false; bOpeningBlocked=false; return; }
 if (!CanOperate() || !IsUserInReach(Operator.Get())) { Release(Operator.Get()); return; }
 if (!bOpeningRequested || Dt<=0.f) return;
 const float NextFraction=FMath::Min(1.f,OpenFraction+Dt/FMath::Max(.1f,OpenSeconds));
 bOpeningBlocked=!CanSweepLeaf(OpenFraction,NextFraction,!bOpeningBlocked);
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

FYUFSTimelineDoorSnapshot AYUFSInteractionDoor::BuildTimelineSnapshot() const
{
 FYUFSTimelineDoorSnapshot Snapshot;
 Snapshot.DoorId=GetFName(); Snapshot.OpenFraction=OpenFraction; Snapshot.SwingDirection=SwingDirection;
 Snapshot.bVisible=!IsHidden(); return Snapshot;
}
void AYUFSInteractionDoor::ApplyTimelineSnapshot(const FYUFSTimelineDoorSnapshot& Snapshot)
{
 // Observation only: never TryUse, sweep the live leaf, or create a new reservation.
 bTimelinePresentation=true; Operator.Reset(); bOpeningRequested=false; bOpeningBlocked=false;
 OpenFraction=FMath::Clamp(Snapshot.OpenFraction,0.f,1.f); SwingDirection=Snapshot.SwingDirection<0.f?-1.f:1.f;
 const float Eased=OpenFraction*OpenFraction*(3.f-2.f*OpenFraction);
 LeafPivot->SetRelativeRotation(FRotator(0,FMath::Abs(OpenAngle)*SwingDirection*Eased,0));
 PassageBlocker->SetCollisionEnabled(IsOpen()?ECollisionEnabled::NoCollision:ECollisionEnabled::QueryAndPhysics);
 SetActorHiddenInGame(!Snapshot.bVisible);
 SetActorEnableCollision(Snapshot.bVisible);
}
