#include "NPC/Integration/YUFSNpcEnvironmentInteraction.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Integration/YUFSTeamIntegrationComponent.h"
#include "NPC/Integration/YUFSBelongingsRetrievalComponent.h"
#include "NPC/Decision/YUFSHumanBehaviorSelectorComponent.h"
#include "NPC/Decision/YUFSIntentComponent.h"
#include "NPC/Decision/YUFSBeliefComponent.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "NPC/Animation/YUFSActionAnimationComponent.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "Fire/YUFSInteractionDoor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Core/YUFSObservation.h"

UYUFSNpcEnvironmentInteraction::UYUFSNpcEnvironmentInteraction() { PrimaryComponentTick.bCanEverTick=false; }
namespace
{
 /** True when the route from From through Points passes through this doorway (crosses the leaf plane between the jambs). */
 bool RouteGoesThroughDoor(const FVector& From, const TArray<FVector>& Points, int32 FirstPoint, const AYUFSInteractionDoor* Door)
 {
  const FTransform DoorFrame=Door->GetActorTransform();
  FVector Previous=DoorFrame.InverseTransformPosition(From);
  for (int32 Index=FMath::Max(0,FirstPoint); Index<Points.Num(); ++Index)
  {
   const FVector Local=DoorFrame.InverseTransformPosition(Points[Index]);
   // Local X is the leaf normal, local Y runs 0..100 across the (scaled) leaf.
   if ((Previous.X<=0.f)!=(Local.X<=0.f) && !FMath::IsNearlyEqual(Previous.X,Local.X))
   {
    const float Across=FMath::Lerp(Previous.Y,Local.Y,Previous.X/(Previous.X-Local.X));
    if (Across>=-10.f && Across<=110.f) return true;
   }
   Previous=Local;
  }
  return false;
 }
}
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
FVector UYUFSNpcEnvironmentInteraction::GetTarget() const { return Person.IsValid()?Person->GetActorLocation():GetOwner()->GetActorLocation(); }
bool UYUFSNpcEnvironmentInteraction::IsSlowingForDoor() const
{
 return IsOperatingDoor() && Door.IsValid() && !Door->IsPassageClear();
}
void UYUFSNpcEnvironmentInteraction::Observe(float Dt)
{
 Npc=Cast<AYUFSEvacuationNPC>(GetOwner());
 if (!Npc.IsValid() || bUseExternalExecutor) return;
 Scan-=Dt; if (Scan>0 || bActive) return; Scan=.15f;
 Door.Reset(); Person.Reset();
 auto* Nav=Npc->GetNavigator();
 // The door the route goes through next, noticed DoorLookAheadCm ahead so that it can be opened
 // on the way. Only a route that actually passes through the doorway counts: a door beside the
 // route (walking along a corridor wall full of room doors) is not on it, and a passer-by grabbing
 // it blocked the occupants trying to come out through it.
 if (Nav && Nav->IsFollowingPath() && !Nav->GetCurrentPathPoints().IsEmpty())
 {
  const TArray<FVector>& Points=Nav->GetCurrentPathPoints();
  TArray<FVector> Ahead;
  FVector Last=Npc->GetActorLocation(); float Travel=0.f;
  for (int32 Index=Nav->GetCurrentWaypointIndex(); Index<Points.Num() && Travel<DoorLookAheadCm; ++Index)
  { Travel+=FVector::Dist2D(Last,Points[Index]); Ahead.Add(Points[Index]); Last=Points[Index]; }
  float Best=FLT_MAX;
  for (TActorIterator<AYUFSInteractionDoor> It(GetWorld()); It; ++It)
  {
   const float Distance=FVector::DistSquared2D(It->GetActorLocation(),Npc->GetActorLocation());
   if (Distance>=Best || Distance>FMath::Square(DoorLookAheadCm+150.f)
       || FMath::Abs(It->GetActorLocation().Z-Npc->GetActorLocation().Z)>200.f) continue;
   if (!RouteGoesThroughDoor(Npc->GetActorLocation(),Ahead,0,*It) || !Visible(*It)) continue;
   Door=*It; Best=Distance;
  }
  // Already open (someone else opened it): just go through the middle, not along a jamb.
  if (Door.IsValid() && Door->IsPassageClear() && SteeredDoor.Get()!=Door.Get())
  {
   Nav->SteerThroughDoorway(Door->GetActorTransform());
   SteeredDoor=Door;
  }
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
   // next evacuation directive or its revision-scoped feedback.
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
  if (WasDoor)
  {
   UE_LOG(LogTemp,Display,TEXT("[EnvironmentInteraction] %s finished %s (door took %.1fs, stood still at it %.1fs)"),
       *Npc->GetName(),*Reason.ToString(),Elapsed,DoorHeldSeconds);
  }
  else
  {
   UE_LOG(LogTemp,Display,TEXT("[EnvironmentInteraction] %s finished %s"),*Npc->GetName(),*Reason.ToString());
  }
 }
 bActive=false; bApproaching=false; bAtDoor=false; ActiveGoal=EYUFSInteractionGoal::None; ActiveTargetId=NAME_None;
 Door.Reset(); Person.Reset();
 Elapsed=Contact=0; SwingBlockedSeconds=DoorHeldSeconds=0.f; RetryAt=GetWorld()->GetTimeSeconds()+2.f; Scan=0;
}
bool UYUFSNpcEnvironmentInteraction::Execute(float Dt, int32 SimFrame)
{
 if (!Npc.IsValid() || bUseExternalExecutor) { Cancel(); return false; }
 if (Npc->GetBehaviorStateMachine()->IsIncapacitated()) { Cancel(); return false; }
 auto* Team=Npc->GetTeamIntegrationComponent(); const auto& Directive=Team->GetInteractionDirective();
 // A usable door on the evacuation route remains necessary during emergency
 // escape. Risk interrupts optional helping, not the escape door.
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
   if (Npc->GetBelongingsRetrievalComponent()->IsActive() || Npc->GetBeliefComponent()->HasVerifiedOfficialInstruction()) return false;
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
  // Open: walk on through it (no standing at the door for one more tick).
  if (Door->IsPassageClear()) { Finish(true,TEXT("DoorOpened")); return false; }
  const float PlaneDistance=FMath::Abs(FVector::DotProduct(
      Npc->GetActorLocation()-Door->GetActorLocation(),Door->GetActorForwardVector()));
  auto* Nav=Npc->GetNavigator();
  const bool bWalkingRoute=Nav && Nav->IsFollowingPath();
  if (!Door->CanOperate()) { Finish(false,TEXT("DoorLockedHotOrOutOfReach")); return true; }
  if (!Door->IsUserInReach(Npc.Get()))
  {
   // Still walking up to it (slowed meanwhile, see IsSlowingForDoor).
   if (bWalkingRoute && Elapsed<DoorApproachSeconds) return false;
   Finish(false,TEXT("DoorLockedHotOrOutOfReach")); return true;
  }
  if (!Door->TryUse(Npc.Get()))
  {
   // Someone else is opening it: walk up behind them and wait at the leaf, never push past them.
   Contact=0.f;
   if (bWalkingRoute && PlaneDistance>DoorHoldDistanceCm+35.f) return false;
   UpdateLocalPose(Door->GetHandleLocation(),EYUFSAction::Idle,Dt);
   DoorHeldSeconds+=Dt;
   return true;
  }
  if (!bAtDoor)
  {
   // Within reach: the hand goes to the handle and the leaf starts moving while the NPC keeps
   // walking. Stopping to face the handle first made every door a stop of 1.5-2 s.
   bAtDoor=true;
   if (Nav) Nav->SteerThroughDoorway(Door->GetActorTransform());
   SteeredDoor=Door;
   UE_LOG(LogTemp,Display,TEXT("[EnvironmentInteraction] %s at door %s, %.0f cm from the leaf after %.1fs"),
       *Npc->GetName(),*Door->GetName(),PlaneDistance,Elapsed);
  }
  // Someone standing where the leaf goes keeps it shut: let go after a while and try again later,
  // pushing on for the whole interaction budget looked like grinding against the door.
  SwingBlockedSeconds=Door->IsOpeningBlocked()?SwingBlockedSeconds+Dt:0.f;
  if (SwingBlockedSeconds>=DoorSwingBlockedGiveUpSeconds) { Finish(false,TEXT("DoorSwingBlocked")); return true; }
  // Already at the leaf and it is not open yet: wait there, reaching for it, for the last moment.
  if (!bWalkingRoute || PlaneDistance<=DoorHoldDistanceCm)
  {
   UpdateLocalPose(Door->GetHandleLocation(),EYUFSAction::GatherBelongings,Dt);
   DoorHeldSeconds+=Dt;
   return true;
  }
  return false;
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
  // The recipient's own knowledge, not global unobserved FDS cells, constrains
  // the exit it will resume toward. This does not claim the whole route is known safe.
  bool Safe=RecipientNav && Person->TryGetNearestKnownExit(Exit);
  if (Safe)
  {
   auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Person->GetActorLocation(),Exit,Person.Get());
   Safe=Path && Path->IsValid() && !Path->IsPartial() && !Path->PathPoints.IsEmpty()
       && !RecipientNav->IsKnownPathDangerous(Path->PathPoints);
  }
  if (!Safe) { Finish(false,TEXT("NoKnownExitForGuidance")); return true; }
  Other->bNeedsAssistance=false;
  Person->ReceivePeerGuidance(); // The recipient's JJW state machine decides; this is not an official command.
  Person->GetHumanBehaviorSelector()->RequestReselection(TEXT("ReceivedNearbyGuidance"));
  Finish(true,TEXT("PersonGuidedToEvacuate"));
 }
 return true;
}
void UYUFSNpcEnvironmentInteraction::Cancel() { if (bActive) Finish(false,TEXT("InteractionCancelled")); }
void UYUFSNpcEnvironmentInteraction::EndPlay(const EEndPlayReason::Type Reason) { Cancel(); Super::EndPlay(Reason); }
