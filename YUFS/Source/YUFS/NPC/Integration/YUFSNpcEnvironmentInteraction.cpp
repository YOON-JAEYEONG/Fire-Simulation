#include "NPC/Integration/YUFSNpcEnvironmentInteraction.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Integration/YUFSTeamIntegrationComponent.h"
#include "NPC/Integration/YUFSNpcSuppressionComponent.h"
#include "NPC/Decision/YUFSHumanBehaviorSelectorComponent.h"
#include "NPC/Decision/YUFSIntentComponent.h"
#include "NPC/Decision/YUFSBeliefComponent.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "NPC/Animation/YUFSActionAnimationComponent.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "Fire/YUFSInteractionDoor.h"
#include "Level/YUFSLevelDataManager.h"
#include "GameFramework/CharacterMovementComponent.h"
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
  Animation->ApplyAction(Action,EYUFSBehaviorState::Normal);
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
 if (Target->IsA<AYUFSInteractionDoor>()) Point.Z+=90.f;
 if (FVector::DistSquared(Npc->GetActorLocation(),Point)>FMath::Square(SearchRadius)
     || FMath::Abs(Npc->GetActorLocation().Z-Point.Z)>160.f) return false;
 FHitResult Hit;
 FCollisionQueryParams Params(SCENE_QUERY_STAT(EnvironmentInteractionSight),false,Npc.Get());
 return !GetWorld()->LineTraceSingleByChannel(Hit,Npc->GetActorLocation()+FVector(0,0,45),Point,ECC_Visibility,Params) || Hit.GetActor()==Target;
}
FVector UYUFSNpcEnvironmentInteraction::GetTarget() const { return Person.IsValid()?Person->GetActorLocation():GetOwner()->GetActorLocation(); }
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
  FVector Direction=(Nav->GetNextWaypoint()-Npc->GetActorLocation()).GetSafeNormal2D();
  const FVector Start=Npc->GetActorLocation(), End=Start+Direction*140.f;
  FHitResult Hit; FCollisionQueryParams Params(SCENE_QUERY_STAT(DoorAhead),false,Npc.Get());
  if (GetWorld()->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility,Params)) Door=Cast<AYUFSInteractionDoor>(Hit.GetActor());
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
 if (Door.IsValid()) Door->Release(GetOwner());
 if (Person.IsValid()) if (auto* Other=Person->FindComponentByClass<UYUFSNpcEnvironmentInteraction>())
  if (Other->Helper.Get()==GetOwner()) Other->Helper.Reset();
 if (Npc.IsValid())
 {
  FYUFSTeamRequestFeedback Feedback; Feedback.RequestRevision=RequestRevision;
  Feedback.Status=Success?EYUFSTeamRequestStatus::Completed:EYUFSTeamRequestStatus::Failed; Feedback.Reason=Reason;
  Npc->GetTeamIntegrationComponent()->SubmitInteractionFeedback(Feedback);
  Npc->GetHumanBehaviorSelector()->RequestReselection(Reason);
  Npc->GetNavigator()->ClearPath();
  UE_LOG(LogTemp,Display,TEXT("[EnvironmentInteraction] %s finished %s"),*Npc->GetName(),*Reason.ToString());
 }
 bActive=false; bApproaching=false; ActiveGoal=EYUFSInteractionGoal::None; ActiveTargetId=NAME_None;
 Elapsed=Contact=0; RetryAt=GetWorld()->GetTimeSeconds()+2.f; Scan=0;
}
bool UYUFSNpcEnvironmentInteraction::Execute(float Dt, int32 SimFrame)
{
 if (!Npc.IsValid() || bUseExternalExecutor) { Cancel(); return false; }
 if (Npc->GetBehaviorStateMachine()->IsIncapacitated() || Npc->GetBehaviorStateMachine()->IsCrawling()
     || Npc->GetBeliefComponent()->HasImmediateLifeRisk()) { Cancel(); return false; }
 auto* Team=Npc->GetTeamIntegrationComponent(); const auto& Directive=Team->GetInteractionDirective();
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
   if (!Door->TryUse(Npc.Get())) { Finish(false,TEXT("DoorLockedHotBusyOrOutOfReach")); return true; }
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
  bApproaching=false; Npc->GetCharacterMovement()->StopMovementImmediately();
  // Existing compatible hand-use sequence is a temporary reach gesture, not door-handle IK.
  UpdateLocalPose(Door->GetActorLocation()+Door->GetActorRightVector()*70.f,EYUFSAction::GatherBelongings,Dt);
  if (Door->IsOpen()) Finish(true,TEXT("DoorOpened"));
  else if (!Door->TryUse(Npc.Get())) Finish(false,TEXT("DoorBecameUnavailable"));
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
  FVector Exit; auto* Level=Person->GetLevelDataManager();
  // Guidance only: never heal, teleport, or claim to carry an incapacitated person.
  bool Safe=Level && Level->TryGetNearestSafeExit(Person->GetActorLocation(),SimFrame,Exit);
  if (Safe)
  {
   auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Person->GetActorLocation(),Exit,Person.Get());
   Safe=Path && Path->IsValid() && !Path->IsPartial();
   if (Safe) for (const FVector& Point:Path->PathPoints) if (Level->IsLocationDangerous(Point,SimFrame)) Safe=false;
  }
  if (!Safe) { Finish(false,TEXT("NoKnownExitForGuidance")); return true; }
  Other->bNeedsAssistance=false;
  Person->GetIntentComponent()->ResumeEvacuationAfterInteraction(true);
  Person->GetHumanBehaviorSelector()->RequestReselection(TEXT("ReceivedNearbyGuidance"));
  Finish(true,TEXT("PersonGuidedToEvacuate"));
 }
 return true;
}
void UYUFSNpcEnvironmentInteraction::Cancel() { if (bActive) Finish(false,TEXT("InteractionCancelled")); }
void UYUFSNpcEnvironmentInteraction::EndPlay(const EEndPlayReason::Type Reason) { Cancel(); Super::EndPlay(Reason); }
