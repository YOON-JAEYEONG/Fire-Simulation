#include "NPC/Integration/YUFSNpcEnvironmentInteraction.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Integration/YUFSTeamIntegrationComponent.h"
#include "NPC/Integration/YUFSNpcSuppressionComponent.h"
#include "NPC/Decision/YUFSHumanBehaviorSelectorComponent.h"
#include "NPC/Decision/YUFSIntentComponent.h"
#include "NPC/Decision/YUFSBeliefComponent.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "NPC/Animation/YUFSActionAnimationComponent.h"
#include "NPC/Navigation/YUFSLocalMovementComponent.h"
#include "NPC/Navigation/YUFSExitKnowledgeComponent.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "Fire/YUFSInteractionDoor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Core/YUFSObservation.h"

UYUFSNpcEnvironmentInteraction::UYUFSNpcEnvironmentInteraction() { PrimaryComponentTick.bCanEverTick=false; }
bool UYUFSNpcEnvironmentInteraction::TryReserveHelper(AYUFSEvacuationNPC* Candidate)
{
 if (!IsValid(Candidate) || Candidate==GetOwner() || !NeedsHelp() || (Helper.IsValid() && Helper.Get()!=Candidate)) return false;
 Helper=Candidate; return true;
}
void UYUFSNpcEnvironmentInteraction::ReleaseHelper(AYUFSEvacuationNPC* Candidate) { if (Helper.Get()==Candidate) Helper.Reset(); }
bool UYUFSNpcEnvironmentInteraction::IsReceivingAssistance() const { return Helper.IsValid(); }
AYUFSEvacuationNPC* UYUFSNpcEnvironmentInteraction::GetAssistingNPC() const { return Helper.Get(); }
bool UYUFSNpcEnvironmentInteraction::IsReceivingContactAssistance() const
{
 if (!Helper.IsValid() || !NeedsHelp()) return false;
 const auto* Executor=Helper->FindComponentByClass<UYUFSNpcEnvironmentInteraction>();
 return Executor && Executor->bActive && !Executor->bApproaching && Executor->Person.Get()==GetOwner()
     && FVector::Dist2D(Helper->GetActorLocation(),GetOwner()->GetActorLocation())<=140.f
     && FMath::Abs(Helper->GetActorLocation().Z-GetOwner()->GetActorLocation().Z)<=160.f;
}
void UYUFSNpcEnvironmentInteraction::UpdateLocalPose(const FVector& FacingTarget, EYUFSAction Action, float Dt)
{
 if (!Npc.IsValid() || Npc->bUseExternalMotionDriver) return;
 const FVector Direction=(FacingTarget-Npc->GetActorLocation()).GetSafeNormal2D();
 if (!Direction.IsNearlyZero())
 {
  const FRotator Desired(0.f,Direction.Rotation().Yaw,0.f);
  Npc->SetActorRotation(FMath::RInterpConstantTo(Npc->GetActorRotation(),Desired,Dt,180.f));
 }
 if (auto* Animation=Npc->GetActionAnimationComponent())
  Animation->ApplyAction(Action,Npc->GetBehaviorStateMachine()->GetCurrentState());
}
bool UYUFSNpcEnvironmentInteraction::NeedsHelp() const
{
 const auto* OwnerNpc=Cast<AYUFSEvacuationNPC>(GetOwner());
 return OwnerNpc && bAcceptsAssistance && !OwnerNpc->GetBehaviorStateMachine()->IsIncapacitated()
     && !OwnerNpc->GetBehaviorStateMachine()->IsCrawling()
     && (bNeedsAssistance || OwnerNpc->GetHumanBehaviorSelector()->GetCurrentDecision().Behavior==EYUFSHighLevelBehavior::Freeze);
}
bool UYUFSNpcEnvironmentInteraction::Visible(AActor* Target) const
{
 if (!Npc.IsValid() || !IsValid(Target)) return false;
 FVector Point=Target->GetActorLocation();
 if (const auto* TargetDoor=Cast<AYUFSInteractionDoor>(Target)) Point=TargetDoor->GetHandleLocation();
 if (FVector::DistSquared(Npc->GetActorLocation(),Point)>FMath::Square(SearchRadius)
     || FMath::Abs(Npc->GetActorLocation().Z-Point.Z)>160.f) return false;
 FHitResult Hit;
 FCollisionQueryParams Params(SCENE_QUERY_STAT(EnvironmentInteractionSight),false,Npc.Get());
 return !GetWorld()->LineTraceSingleByChannel(Hit,Npc->GetActorLocation()+FVector(0,0,45),Point,ECC_Visibility,Params) || Hit.GetActor()==Target;
}
bool UYUFSNpcEnvironmentInteraction::WantsThisDoor(const AYUFSInteractionDoor& AtDoor) const
{
 if (!Npc.IsValid() || bUseExternalExecutor || Npc->bUseExternalNavigationDriver
     || Npc->IsHidden() || !Npc->GetActorEnableCollision() || Npc->GetBehaviorStateMachine()->IsIncapacitated()
     || (!bActive && GetWorld()->GetTimeSeconds()<RetryAt)) return false;
 const bool AlreadyQueued=IsOperatingDoor() && Door.Get()==&AtDoor;
 if ((bActive && !AlreadyQueued) || IsPassingDoor()
     || (!AlreadyQueued && Npc->GetTeamIntegrationComponent()->GetNavigationDirective().Goal==EYUFSNavigationGoal::None))
  return false;
 if (const auto* Knowledge=Npc->GetExitKnowledge(); Knowledge && Knowledge->GetPlannedSearchDoor()==&AtDoor) return true;
 const auto* Nav=Npc->GetNavigator();
 const FVector Feet=Npc->GetActorLocation()-FVector(0,0,Npc->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
 double Distance=0.0;
 if (Nav && Nav->GetCurrentPathPoints().IsValidIndex(Nav->GetCurrentWaypointIndex()))
  return AtDoor.FindPortalOnPath(Feet,Nav->GetCurrentPathPoints(),Nav->GetCurrentWaypointIndex(),300.0,Distance);
 return AlreadyQueued || Door.Get()==&AtDoor;
}
bool UYUFSNpcEnvironmentInteraction::IsFrontOfDoorQueue(AYUFSInteractionDoor& AtDoor) const
{
 const double MyDistance=AtDoor.GetApproachDistance(*Npc.Get());
 for (TActorIterator<AYUFSEvacuationNPC> It(GetWorld());It;++It)
 {
  const auto* Peer=*It;
  if (Peer==Npc.Get() || !AtDoor.IsUserInReach(*It)) continue;
  const double PeerDistance=AtDoor.GetApproachDistance(*Peer);
  const bool OnTie=FMath::Abs(PeerDistance-MyDistance)<=1.0;
  const bool HigherPriority=OnTie
      ? (Peer->GetStableNPCId()==Npc->GetStableNPCId() ? Peer->GetFName().LexicalLess(Npc->GetFName())
          : Peer->GetStableNPCId()<Npc->GetStableNPCId())
      : PeerDistance<MyDistance;
  if (!HigherPriority) continue;
  const auto* Candidate=Peer->FindComponentByClass<UYUFSNpcEnvironmentInteraction>();
  if (!Candidate || Candidate->bUseExternalExecutor || Peer->bUseExternalNavigationDriver
      || Peer->IsHidden() || !Peer->GetActorEnableCollision() || !Candidate->Visible(&AtDoor)) continue;
  if (Candidate->WantsThisDoor(AtDoor)) return false;

  // A committed resident at the threshold may still be finishing their
  // preparation animation. Give them a moment to take the lead, but never
  // reserve a door for someone with no route or hold up a fleeing queue indefinitely.
  const auto* State=Peer->GetBehaviorStateMachine();
  const auto* Nav=Peer->GetNavigator();
  const FVector Plane=AtDoor.GetPortalFloorPoint(0), Normal=AtDoor.GetActorForwardVector();
  const bool SameSide=FVector::DotProduct(Npc->GetActorLocation()-Plane,Normal)
      *FVector::DotProduct(Peer->GetActorLocation()-Plane,Normal)>0.0;
  if (Elapsed<2.25f && SameSide && PeerDistance<=90.f && MyDistance-PeerDistance>=18.f
      && !Candidate->bActive && !Peer->IsPickingUpBelongings() && !Peer->IsReturningForBelongings()
      && State->HasCommittedToEvacuation() && State->GetCurrentState()==EYUFSBehaviorState::Preparing
      && Nav && Nav->GetCurrentPathPoints().IsEmpty()) return false;
 }
 return true;
}
bool UYUFSNpcEnvironmentInteraction::ContinueFirstPassage(float Dt,int32 Frame)
{
 const auto* Opened=PassageDoor.Get();
 auto* Local=Npc->GetLocalMovement();
 if (!Opened || Opened->GetFirstPasser()!=Npc.Get() || Npc->bUseExternalNavigationDriver || !Local)
 {
  PassageDoor.Reset(); return false;
 }
 const double ExitSide=Opened->GetFirstPassageDestinationSide();
 if (!Local->IsRecovering() && !Local->TryDoorwayPassage(*Opened,Frame,ExitSide)) return true;
 // Once walking, keep facing the actual movement. Recomputing the opposite
 // side from the current position would turn the animation back at the threshold.
 const FVector Heading=Npc->GetVelocity().GetSafeNormal2D();
 const FVector Landing=Heading.IsNearlyZero() ? Opened->GetPortalFloorPoint(ExitSide*115.0)
     : Npc->GetActorLocation()+Heading*100.f;
 UpdateLocalPose(Landing,EYUFSAction::HelpOther,Dt);
 Local->TickRecovery(Dt,Frame);
 return true;
}
FVector UYUFSNpcEnvironmentInteraction::GetTarget() const { return Person.IsValid()?Person->GetActorLocation():GetOwner()->GetActorLocation(); }
bool UYUFSNpcEnvironmentInteraction::ClearDoorSwing(int32 Frame, float Dt)
{
 const FVector Location=Npc->GetActorLocation();
 if ((!Door->UsesSlidingMotion() && Door->SwingSide==EYUFSDoorSwingSide::AwayFromOperator)
     || !Door->IsInOpeningSwing(*Npc.Get(),Location))
 {
  DoorClearanceTarget.Reset();
  return true;
 }

 Contact=0.f;
 if (!StepAside(*Door.Get(),EDoorStep::Operator,Frame,Dt,DoorClearanceTarget,NextDoorClearanceSearch))
 {
  if (DoorNoProgress>.3f) Door->RequestRoom(*Npc.Get());
  Npc->GetCharacterMovement()->StopMovementImmediately();
  UpdateLocalPose(Door->GetHandleLocation(),EYUFSAction::Idle,Dt);
 }
 return false;
}

bool UYUFSNpcEnvironmentInteraction::StepAside(const AYUFSInteractionDoor& AtDoor, EDoorStep Step, int32 Frame,
    float Dt, TOptional<FVector>& Target, float& NextSearch)
{
 auto* Local=Npc->GetLocalMovement();
 if (Npc->bUseExternalNavigationDriver || !Local) return false;
 const FVector Location=Npc->GetActorLocation();
 const FVector FootOffset(0,0,Npc->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
 const FVector Feet=Location-FootOffset;
 const FVector Outward=AtDoor.GetActorForwardVector().GetSafeNormal2D()
     *(AtDoor.GetActorTransform().InverseTransformPosition(Location).X>=0.f ? 1.f : -1.f);
 const FVector Along=AtDoor.GetActorRightVector().GetSafeNormal2D();
 const float Now=GetWorld()->GetTimeSeconds();
 auto GoodStep=[&](FVector Candidate,FVector& Projected)
 {
  auto GoodEndpoint=[&](FVector End)
  {
   return !AtDoor.IsInOpeningSwing(*Npc.Get(),End)
       && (Step!=EDoorStep::Operator || AtDoor.IsLocationInReach(End))
       && (Step!=EDoorStep::MakeRoom || !AtDoor.IsInOperatorApproach(*Npc.Get(),End));
  };
  return GoodEndpoint(Candidate+FootOffset)
      && Local->IsCandidateReachable(Candidate,Frame,Projected,&AtDoor)
      && FVector::DistSquared2D(Feet,Projected)>FMath::Square(12.f)
      && GoodEndpoint(Projected+FootOffset);
 };
 if (Target.IsSet() && FVector::DistSquared2D(Feet,Target.GetValue())<=FMath::Square(13.f)) Target.Reset();
 if (Now>=NextSearch)
 {
  NextSearch=Now+.24f;
  FVector Projected;
  if (Target.IsSet() && !GoodStep(Target.GetValue(),Projected)) Target.Reset();
  if (!Target.IsSet())
  {
   // Try the short step back first; diagonals and wall-parallel steps cover narrow rooms.
   for (const float Distance : {45.f,70.f,105.f,145.f})
   {
    for (const FVector& Direction : {Outward,Outward+.65f*Along,Outward-.65f*Along,
         .45f*Outward+Along,.45f*Outward-Along,Along,-Along,-Outward})
    {
     if (!GoodStep(Feet+Direction.GetSafeNormal2D()*Distance,Projected)) continue;
     Target=Projected;
     break;
    }
    if (Target.IsSet()) break;
   }
  }
 }
 if (!Target.IsSet()) return false;
 Npc->GetCharacterMovement()->MaxWalkSpeed=FMath::Min(150.f,Npc->GetDesiredWalkingSpeed());
 Npc->ConsumeMovementInputVector();
 Npc->AddMovementInput((Target.GetValue()-Feet).GetSafeNormal2D(),1.f);
 UpdateLocalPose(Target.GetValue(),EYUFSAction::HelpOther,Dt);
 return true;
}

bool UYUFSNpcEnvironmentInteraction::YieldDoorwaySpace(float Dt,int32 Frame)
{
 bYieldingDoorSpace=false;
 Npc=Cast<AYUFSEvacuationNPC>(GetOwner());
 if (!Npc.IsValid() || bUseExternalExecutor || Npc->bUseExternalNavigationDriver
     || Npc->GetBehaviorStateMachine()->IsIncapacitated())
 {
  YieldDoor.Reset(); YieldClearanceTarget.Reset(); return false;
 }
 const FVector Location=Npc->GetActorLocation();
 auto CanMakeRoom=[&](const AYUFSInteractionDoor& AtDoor)
 {
  return AtDoor.IsRequestingRoom() && AtDoor.IsInOperatorApproach(*Npc.Get(),Location,true);
 };
 auto* Selected=YieldDoor.Get();
 if (Selected && (!Selected->HasOpeningOperator() || Selected->IsReservedBy(Npc.Get())))
 {
  YieldDoor.Reset(); YieldClearanceTarget.Reset(); Selected=nullptr;
 }
 if (!Selected && GetWorld()->GetTimeSeconds()>=NextYieldScan)
 {
  NextYieldScan=GetWorld()->GetTimeSeconds()+.2f;
  double Best=FMath::Square(440.0);
  bool bBestBlocksLeaf=false;
  for (TActorIterator<AYUFSInteractionDoor> It(GetWorld());It;++It)
  {
   const double Distance=FVector::DistSquared(Location,It->GetHandleLocation());
   if (Distance>=FMath::Square(440.0)) continue;
   const bool bBlocksLeaf=It->ShouldYieldOpeningSpace(*Npc.Get());
   if (!bBlocksLeaf && (bBestBlocksLeaf || !CanMakeRoom(**It))) continue;
   if (bBlocksLeaf==bBestBlocksLeaf && Distance>=Best) continue;
   Selected=*It; Best=Distance; bBestBlocksLeaf=bBlocksLeaf;
  }
  if (Selected)
  {
   YieldDoor=Selected; NextYieldSearch=0.f;
  }
 }
 if (!Selected) return false;
 const bool bBlocksLeaf=Selected->ShouldYieldOpeningSpace(*Npc.Get());
 if (!bBlocksLeaf && !CanMakeRoom(*Selected)
     && (!YieldClearanceTarget.IsSet() || FVector::DistSquared2D(Location,YieldClearanceTarget.GetValue())<=FMath::Square(13.f)))
 {
  YieldDoor.Reset(); YieldClearanceTarget.Reset(); return false;
 }
 // Never hold a second door reservation while physically blocking the first.
 if (IsOperatingDoor() && Door.IsValid() && Door.Get()!=Selected && Door->IsReservedBy(Npc.Get()))
 {
  if (!bBlocksLeaf) return false;
  Finish(false,TEXT("YieldingToOtherDoor"));
 }
 const bool bHadTarget=YieldClearanceTarget.IsSet();
 bYieldingDoorSpace=StepAside(*Selected,bBlocksLeaf?EDoorStep::ClearLeaf:EDoorStep::MakeRoom,
     Frame,Dt,YieldClearanceTarget,NextYieldSearch);
 if (!bHadTarget && bYieldingDoorSpace)
  UE_LOG(LogTemp,Log,TEXT("[YUFS][DoorCourtesy] npc=%s %s %s"),*Npc->GetName(),
      bBlocksLeaf?TEXT("clears rail at"):TEXT("makes room at"),*Selected->GetName());
 return bYieldingDoorSpace;
}
void UYUFSNpcEnvironmentInteraction::Observe(float Dt)
{
 Npc=Cast<AYUFSEvacuationNPC>(GetOwner());
 if (!Npc.IsValid() || bUseExternalExecutor) return;
 Scan-=Dt; if (Scan>0 || bActive) return; Scan=.5f;
 Door.Reset(); Person.Reset();
 const auto* Nav=Npc->GetNavigator();
 // Only doors physically in front of the next path segment, never nearest-room doors.
 if (Nav && !Nav->GetCurrentPathPoints().IsEmpty())
 {
  const FVector Segment=Nav->GetNextWaypoint()-Npc->GetActorLocation();
  const FVector Direction=Segment.GetSafeNormal2D();
  // Do not look through a path corner into a room that the route does not enter.
  const FVector Start=Npc->GetActorLocation(), End=Start+Direction*FMath::Min(140.f,Segment.Size2D());
  FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(DoorAhead),false,Npc.Get());
  const float Radius=Npc->GetCapsuleComponent()->GetScaledCapsuleRadius();
  if (!Direction.IsNearlyZero() && GetWorld()->SweepSingleByChannel(Hit,Start,End,FQuat::Identity,ECC_Visibility,
      FCollisionShape::MakeSphere(Radius),Params)) Door=Cast<AYUFSInteractionDoor>(Hit.GetActor());
  if (Door.IsValid() && (!Door->IsUserInReach(Npc.Get()) || !Visible(Door.Get()))) Door.Reset();
   if (auto* Knowledge=Npc->GetExitKnowledge())
   {
    if (Door.IsValid()) Knowledge->RememberLocalDoor(Door.Get());
    else if (auto* Portal=Knowledge->FindKnownDoorAlongPath(Nav->GetCurrentPathPoints(), Nav->GetCurrentWaypointIndex());
      IsValid(Portal) && Visible(Portal)) Door=Portal;
   }
  if (!Door.IsValid())
  {
   // Recognize the exact door on the near route even if a neighbor blocked the
   // first look at the leaf. Never substitute a more visible neighboring door.
   const FVector Feet=Npc->GetActorLocation()-FVector(0,0,Npc->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
   double BestDistance=301.0;
   for (TActorIterator<AYUFSInteractionDoor> It(GetWorld());It;++It)
   {
    double Distance=0.0;
    if (It->IsPassageClear() || !It->IsUserInReach(Npc.Get())
        || !It->FindPortalOnPath(Feet,Nav->GetCurrentPathPoints(),Nav->GetCurrentWaypointIndex(),300.0,Distance)
        || Distance>=BestDistance || !Visible(*It)) continue;
    Door=*It; BestDistance=Distance;
   }
  }
 }
  if ((!Door.IsValid() || Door->IsPassageClear()) && Npc->GetExitKnowledge())
  {
   auto* Portal=Npc->GetExitKnowledge()->GetPlannedSearchDoor();
   if (IsValid(Portal) && Portal->CanOperate() && Portal->IsUserInReach(Npc.Get()) && Visible(Portal)) Door=Portal;
  }
 float Best=FLT_MAX;
 for (TActorIterator<AYUFSEvacuationNPC> It(GetWorld()); It; ++It)
 {
  auto* Other=It->FindComponentByClass<UYUFSNpcEnvironmentInteraction>();
  const float Distance=FVector::DistSquared(It->GetActorLocation(),Npc->GetActorLocation());
  if (*It!=Npc.Get() && Other && Other->NeedsHelp() && !Other->Helper.IsValid() && Distance<Best && Visible(*It))
  { Person=*It; Best=Distance; }
 }
 auto* Team=Npc->GetTeamIntegrationComponent(); auto Next=Team->GetInteractionOpportunities();
 const FName DoorId=Door.IsValid()?Door->GetFName():NAME_None, PersonId=Person.IsValid()?Person->GetFName():NAME_None;
 const bool Required=Door.IsValid()&&!Door->IsOpen();
 if (Next.DoorStableId!=DoorId || Next.bDoorActionRequired!=Required || Next.AssistPersonStableId!=PersonId
     || (Person.IsValid() && !Next.AssistPersonLocation.Equals(Person->GetActorLocation(),30.f)))
 {
  Next.DoorStableId=DoorId; Next.bDoorActionRequired=Required;
  Next.DoorUseLocation=Npc->GetActorLocation();
  Next.bAssistPersonKnown=Person.IsValid(); Next.AssistPersonStableId=PersonId; Next.AssistPersonLocation=GetTarget();
  ++Next.KnowledgeRevision; Team->SubmitInteractionOpportunities(Next);
 }
}
void UYUFSNpcEnvironmentInteraction::Finish(bool Success,FName Reason)
{
 const bool WasDoor=ActiveGoal==EYUFSInteractionGoal::OpenDoor;
 if (Door.IsValid()) Door->Release(GetOwner());
 if (Person.IsValid()) if (auto* Other=Person->FindComponentByClass<UYUFSNpcEnvironmentInteraction>())
  if (Other->Helper.Get()==GetOwner()) Other->Helper.Reset();
 if (Npc.IsValid())
 {
  FYUFSTeamRequestFeedback Feedback; Feedback.RequestRevision=RequestRevision;
  const bool WasCancelled=Reason==TEXT("InteractionCancelled");
  Feedback.Status=Success?EYUFSTeamRequestStatus::Completed:
      (WasCancelled?EYUFSTeamRequestStatus::Cancelled:EYUFSTeamRequestStatus::Failed); Feedback.Reason=Reason;
  auto* Team=Npc->GetTeamIntegrationComponent();
  Team->SubmitInteractionFeedback(Feedback);
  if (WasDoor)
  {
   // Opening is a sub-action of the existing route, not a new destination.
   // Preserve the route/waypoint so local traffic coordination can resume it.
   // Clear even on failure/cancel: a stale door opportunity must not mask the
   // next suppression/evacuation directive or its revision-scoped feedback.
   auto Next=Team->GetInteractionOpportunities();
   Next.DoorStableId=NAME_None; Next.bDoorActionRequired=false;
   ++Next.KnowledgeRevision; Team->SubmitInteractionOpportunities(Next);
   if (!Success && !WasCancelled)
   {
    FYUFSTeamRequestFeedback Blocked;
    Blocked.RequestRevision=Team->GetNavigationDirective().Revision;
    Blocked.Status=EYUFSTeamRequestStatus::Blocked; Blocked.Reason=Reason;
    Blocked.ResolvedLocation=Npc->GetActorLocation(); Team->SubmitNavigationFeedback(Blocked);
   }
  }
  if (!WasDoor || !Success) Npc->GetHumanBehaviorSelector()->RequestReselection(Reason);
  if (!WasDoor) Npc->GetNavigator()->ClearPath();
  UE_LOG(LogTemp,Display,TEXT("[EnvironmentInteraction] %s finished %s"),*Npc->GetName(),*Reason.ToString());
 }
 bActive=false; bApproaching=false; ActiveGoal=EYUFSInteractionGoal::None; ActiveTargetId=NAME_None;
 Door.Reset(); Person.Reset();
 DoorClearanceTarget.Reset(); NextDoorClearanceSearch=0.f;
 DoorNoProgress=LastDoorFraction=0.f;
 Elapsed=Contact=0; RetryAt=GetWorld()->GetTimeSeconds()+2.f; Scan=0;
}
bool UYUFSNpcEnvironmentInteraction::Execute(float Dt, int32 SimFrame)
{
 if (!Npc.IsValid() || bUseExternalExecutor) { Cancel(); return false; }
 if (Npc->GetBehaviorStateMachine()->IsIncapacitated()) { Cancel(); return false; }
 if (PassageDoor.IsValid() && ContinueFirstPassage(Dt,SimFrame)) return true;
 auto* Team=Npc->GetTeamIntegrationComponent(); const auto& Directive=Team->GetInteractionDirective();
 // A usable door on the evacuation route remains necessary during emergency
 // escape. Risk interrupts optional helping/suppression, not the escape door.
 if (Directive.Goal!=EYUFSInteractionGoal::OpenDoor && !Npc->AllowsOptionalInteractions()
     && !Npc->bInteractionPreviewControlled)
 { Cancel(); return false; }
 if (bActive)
 {
  // A changed reason or refreshed location is not a different interaction.
  // Preserve its reservation and contact progress unless its actual identity changes.
  if (Directive.Goal!=ActiveGoal || Directive.TargetStableId!=ActiveTargetId) { Cancel(); return false; }
  RequestRevision=Directive.Revision;
 }
 if (!bActive)
 {
  if (GetWorld()->GetTimeSeconds()<RetryAt) return false;
  if (Directive.Goal==EYUFSInteractionGoal::OpenDoor && Door.IsValid() && Directive.TargetStableId==Door->GetFName())
  {
   RequestRevision=Directive.Revision;
   Person.Reset();
  }
  else if (Directive.Goal==EYUFSInteractionGoal::AssistPerson && Person.IsValid() && Directive.TargetStableId==Person->GetFName())
  {
   if (Npc->GetSuppressionComponent()->IsActive() || Npc->GetBeliefComponent()->HasVerifiedOfficialInstruction()) return false;
   auto* Other=Person->FindComponentByClass<UYUFSNpcEnvironmentInteraction>();
   RequestRevision=Directive.Revision;
   auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Npc->GetActorLocation(),Person->GetActorLocation(),Npc.Get());
   if (!Other || !Other->NeedsHelp() || Other->Helper.IsValid() || !Path || !Path->IsValid() || Path->IsPartial())
   { Finish(false,TEXT("AssistTargetUnavailable")); return true; }
   if (!Other->TryReserveHelper(Npc.Get())) { Finish(false,TEXT("PersonReservedByAnotherHelper")); return true; }
   Door.Reset();
  }
  else return false;
  bActive=true; ActiveGoal=Directive.Goal; ActiveTargetId=Directive.TargetStableId; Elapsed=Contact=0;
  UE_LOG(LogTemp,Display,TEXT("[EnvironmentInteraction] %s started goal=%d"),*Npc->GetName(),int32(Directive.Goal));
 }
 Elapsed+=Dt;
 if (Elapsed>20.f) { Finish(false,TEXT("InteractionTimeout")); return true; }
 if (Door.IsValid())
 {
  bApproaching=false;
  if (Door->IsPassageClear())
  {
   auto* Opened=Door.Get();
   const auto* First=Opened->GetFirstPasser();
   Npc->GetCharacterMovement()->StopMovementImmediately();
   if (First && First!=Npc.Get()) return true;
   const bool MyEvacuationPassage=First==Npc.Get()
       && (Npc->GetCurrentIntent()==EYUFSIntent::CommitEvac || Npc->bInteractionPreviewControlled);
   Finish(true,TEXT("DoorOpened"));
   if (MyEvacuationPassage)
   {
    PassageDoor=Opened;
    ContinueFirstPassage(Dt,SimFrame);
   }
   return true;
  }
  if (!Door->CanOperate() || !Door->IsUserInReach(Npc.Get()))
  { Npc->GetCharacterMovement()->StopMovementImmediately(); Finish(false,TEXT("DoorLockedHotOrOutOfReach")); return true; }
  const bool bAlreadyOperator=Door->IsReservedBy(Npc.Get());
  if (!bAlreadyOperator && !Door->HasOpeningOperator() && !IsFrontOfDoorQueue(*Door.Get()))
  {
   // Hold behind the lead resident instead of backing them out of the doorway.
   Contact=0.f;
   Npc->GetCharacterMovement()->StopMovementImmediately();
   UpdateLocalPose(Door->GetHandleLocation(),EYUFSAction::Idle,Dt);
   return true;
  }
  if (!Door->TryReserve(Npc.Get()))
  {
   Contact=0.f;
   if ((DoorClearanceTarget.IsSet() || Door->IsInOperatorApproach(*Npc.Get(),Npc->GetActorLocation()))
       && StepAside(*Door.Get(),EDoorStep::MakeRoom,SimFrame,Dt,DoorClearanceTarget,NextDoorClearanceSearch))
    return true;
   Npc->GetCharacterMovement()->StopMovementImmediately();
   UpdateLocalPose(Door->GetHandleLocation(),EYUFSAction::Idle,Dt);
   return true; // Do not crowd/push the NPC currently operating this same door.
  }
  const FVector Position=Npc->GetActorLocation();
  const float Fraction=Door->GetOpenFraction();
  if (!bAlreadyOperator || Fraction>LastDoorFraction+.015f
      || FVector::DistSquared2D(Position,LastDoorProgressPosition)>FMath::Square(15.f))
  {
   DoorNoProgress=0.f; LastDoorFraction=Fraction; LastDoorProgressPosition=Position;
   if (!bAlreadyOperator)
   {
    DoorClearanceTarget.Reset(); NextDoorClearanceSearch=0.f;
    UE_LOG(LogTemp,Log,TEXT("[YUFS][DoorQueue] door=%s lead=%s distance=%.1f"),
        *Door->GetName(),*Npc->GetName(),Door->GetApproachDistance(*Npc.Get()));
   }
  }
  else DoorNoProgress+=Dt;
  if (DoorNoProgress>4.f)
  {
   Finish(false,TEXT("DoorNoProgress")); return true;
  }
  if (!ClearDoorSwing(SimFrame,Dt)) return true;
  Npc->GetCharacterMovement()->StopMovementImmediately();
  const FVector Handle=Door->GetHandleLocation();
  const FVector Facing=(Handle-Npc->GetActorLocation()).GetSafeNormal2D();
  const bool Aligned=Facing.IsNearlyZero() || FVector::DotProduct(Npc->GetActorForwardVector().GetSafeNormal2D(),Facing)
      >=FMath::Cos(FMath::DegreesToRadians(DoorFacingToleranceDegrees));
  // Align first, then play the compatible reach gesture. No hand IK is claimed.
  UpdateLocalPose(Handle,Aligned?EYUFSAction::GatherBelongings:EYUFSAction::Idle,Dt);
  if (!Aligned) { Contact=0.f; return true; }
  Contact+=Dt;
  if (Contact>=DoorReachSeconds && !Door->TryUse(Npc.Get())) Finish(false,TEXT("DoorBecameUnavailable"));
  return true;
 }
 if (!Person.IsValid()) { Finish(false,TEXT("PersonLost")); return true; }
 auto* Other=Person->FindComponentByClass<UYUFSNpcEnvironmentInteraction>();
 if (!Other || !Other->NeedsHelp() || !Visible(Person.Get()) || Npc->GetBeliefComponent()->HasVerifiedOfficialInstruction()
     || Npc->GetLastObservation().RiskLevel>=.65f)
 { Finish(false,TEXT("AssistanceUnsafeOrNoLongerNeeded")); return true; }
 bApproaching=FVector::Dist2D(Npc->GetActorLocation(),Person->GetActorLocation())>140.f;
 if (bApproaching)
 {
  Contact=0;
  // Navigation owns facing while walking; only choose the matching visible locomotion.
  if (!Npc->bUseExternalMotionDriver)
   if (auto* Animation=Npc->GetActionAnimationComponent())
    Animation->ApplyAction(EYUFSAction::HelpOther,EYUFSBehaviorState::Normal);
  return false;
 }
 Npc->GetCharacterMovement()->StopMovementImmediately();
 UpdateLocalPose(Person->GetActorLocation(),EYUFSAction::AlertNearbyOccupants,Dt);
 Contact+=Dt;
 if (Contact>=2.f)
 {
  FVector Exit; const auto* RecipientNav=Person->GetNavigator();
  // Guidance only: never heal, teleport, or claim to carry an incapacitated person.
  // Share the helper's remembered landmark, then check the recipient's personal
  // hazard knowledge. This does not certify the whole route as safe.
  bool Safe=RecipientNav && Npc->TryGetNearestKnownExit(Exit);
  if (Safe)
  {
   auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Person->GetActorLocation(),Exit,Person.Get());
   Safe=Path && Path->IsValid() && !Path->IsPartial() && !Path->PathPoints.IsEmpty()
       && !RecipientNav->IsKnownPathDangerous(Path->PathPoints);
  }
  if (!Safe) { Finish(false,TEXT("NoKnownExitForGuidance")); return true; }
  Other->bNeedsAssistance=false;
  if (!Person->ReceiveExitGuidance(Npc.Get())) { Finish(false,TEXT("ExitGuidanceNotReceived"));return true; }
  Person->GetHumanBehaviorSelector()->RequestReselection(TEXT("ReceivedNearbyGuidance"));
  Finish(true,TEXT("PersonGuidedToEvacuate"));
 }
 return true;
}
void UYUFSNpcEnvironmentInteraction::Cancel()
{
 if (bActive) Finish(false,TEXT("InteractionCancelled"));
 PassageDoor.Reset();
}
void UYUFSNpcEnvironmentInteraction::EndPlay(const EEndPlayReason::Type Reason) { Cancel(); Super::EndPlay(Reason); }
