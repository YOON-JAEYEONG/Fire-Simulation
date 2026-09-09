#include "Debug/YUFSInteractionPreview.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Integration/YUFSNpcEnvironmentInteraction.h"
#include "NPC/Integration/YUFSNpcSuppressionComponent.h"
#include "NPC/Integration/YUFSTeamIntegrationComponent.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "Fire/YUFSInteractionDoor.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NPC/Decision/YUFSIntentComponent.h"
#include "NPC/Decision/YUFSHumanBehaviorSelectorComponent.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "Level/YUFSLevelDataManager.h"
#include "Fire/YUFSBinaryManager.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "EngineUtils.h"

AYUFSInteractionPreview::AYUFSInteractionPreview() { PrimaryActorTick.bCanEverTick=true; }
bool AYUFSInteractionPreview::IsReady() const { return IsValid(User) && IsValid(Recipient); }
void AYUFSInteractionPreview::BeginPlay()
{
 Super::BeginPlay();
 TArray<AYUFSEvacuationNPC*> Residents;
 for(TActorIterator<AYUFSEvacuationNPC> It(GetWorld());It;++It)
 {
  It->SetAnimationShowcaseDebugSuppressed(true);
  if(It->GetActorLocation().Z>300) Residents.Add(*It);
 }
 Residents.Sort([](const AYUFSEvacuationNPC& A,const AYUFSEvacuationNPC& B){return A.GetName()<B.GetName();});
 if(Residents.Num()<2)
 {
  UE_LOG(LogTemp,Error,TEXT("[InteractionPreview] Setup failed: fewer than two second-floor residents."));
  return;
 }
 User=Residents[0]; Recipient=Residents[1];
 // Keep the player's existing free-view pawn, camera, cursor and input mode.
 // The fixture only runs world interactions when the normal Start button fires.
 UE_LOG(LogTemp,Display,TEXT("[InteractionPreview] Ready; waiting for normal Start. Player view unchanged; no overlay or preview camera."));
}
void AYUFSInteractionPreview::StartSequence()
{
 if(bSimulationStarted || !IsReady()) return;
 bSimulationStarted=true;
 bSimulationPaused=false;
 PrepareResidentsOnce();
 // Initial conditions only. Later transitions may never reposition either actor.
 if(!Place(User,FVector(-430,-150,351)) || !Place(Recipient,FVector(-240,230,351)))
 {
  UE_LOG(LogTemp,Error,TEXT("[InteractionPreview] Initial placement failed; returning residents to normal decisions."));
  ReleaseResidentsToEvacuation();
  return;
 }
 Door=GetWorld()->SpawnActor<AYUFSInteractionDoor>(FVector(-480,90,351),FRotator(0,-90,0));
 if(!Door)
 {
  UE_LOG(LogTemp,Error,TEXT("[InteractionPreview] Door spawn failed; returning residents to normal decisions."));
  ReleaseResidentsToEvacuation();
  return;
 }
 Door->OpenSeconds=2.5f;
 User->InteractionPreviewBehavior=EYUFSHighLevelBehavior::EvacuateNearest;
 User->InteractionPreviewDestination=FVector(-430,230,440);
 Stage=EYUFSInteractionPreviewStage::Door;
 UE_LOG(LogTemp,Display,TEXT("[InteractionPreview] Continuous run: open door -> cross -> nearby guidance -> BOTH evacuate. No stage teleports or door deletion."));
}
void AYUFSInteractionPreview::SetSimulationPaused(bool bPaused)
{
 bSimulationPaused=bPaused;
 // The controller pauses the NPCs; the separate door actor needs its own gate.
 if(IsValid(Door)) Door->SetActorTickEnabled(!bPaused);
 UE_LOG(LogTemp,Display,TEXT("[InteractionPreview] Sequence %s."),bPaused ? TEXT("paused") : TEXT("resumed"));
}
bool AYUFSInteractionPreview::Place(AYUFSEvacuationNPC* Npc,const FVector& FloorPoint)
{
 auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()); FNavLocation Point;
 if(!Npc || !Nav || !Nav->ProjectPointToNavigation(FloorPoint,Point,FVector(40,40,65)) || FMath::Abs(Point.Location.Z-FloorPoint.Z)>65) return false;
 Npc->ApplyDistributedSpawnLocation(Point.Location+FVector(0,0,Npc->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2));
 Npc->SetActorRotation(FRotator(0,90,0)); Npc->GetNavigator()->ClearPath();
 Npc->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
 UE_LOG(LogTemp,Display,TEXT("[InteractionPreview] INITIAL placement %s %s"),*Npc->GetName(),*Npc->GetActorLocation().ToString());
 return true;
}
void AYUFSInteractionPreview::PrepareResidentsOnce()
{
 for(auto* Npc:{User.Get(),Recipient.Get()}) if(Npc)
 {
  Npc->EnvironmentInteraction->Cancel(); Npc->EnvironmentInteraction->RequestAssistance(false);
  Npc->GetSuppressionComponent()->Cancel(); Npc->GetSuppressionComponent()->bEnabled=false;
  Npc->SetTimelinePlaybackMode(false);
  Npc->bInteractionPreviewControlled=true; Npc->InteractionPreviewBehavior=EYUFSHighLevelBehavior::WaitObserve;
  Npc->GetCharacterMovement()->StopMovementImmediately(); Npc->GetNavigator()->ClearPath();
  auto Snapshot=Npc->GetTeamIntegrationComponent()->GetInteractionOpportunities();
  Snapshot.bDoorActionRequired=false; Snapshot.DoorStableId=NAME_None;
  Snapshot.bAssistPersonKnown=false; Snapshot.AssistPersonStableId=NAME_None; ++Snapshot.KnowledgeRevision;
  Npc->GetTeamIntegrationComponent()->SubmitInteractionOpportunities(Snapshot);
 }
 Elapsed=0;
}
void AYUFSInteractionPreview::StartHelp()
{
 if(!IsReady() || Stage!=EYUFSInteractionPreviewStage::Door || bSimulationPaused) return;
 Stage=EYUFSInteractionPreviewStage::Help;
 Elapsed=0;
 StartingFeedbackGeneration=User->GetTeamIntegrationComponent()->GetFeedbackGeneration();
 Recipient->EnvironmentInteraction->RequestAssistance(true);
 User->InteractionPreviewBehavior=EYUFSHighLevelBehavior::AssistOther;
 User->GetNavigator()->ClearPath();
 User->EnvironmentInteraction->Observe(.5f);
 UE_LOG(LogTemp,Display,TEXT("[InteractionPreview] Help starts IN PLACE: helper=%s recipient=%s door=%s open=%d."),
  *User->GetActorLocation().ToString(),*Recipient->GetActorLocation().ToString(),*GetNameSafe(Door),Door && Door->IsOpen());
}
void AYUFSInteractionPreview::ReleaseToEvacuation(AYUFSEvacuationNPC* Npc)
{
 if(!IsValid(Npc) || !Npc->bInteractionPreviewControlled) return;
 Npc->EnvironmentInteraction->Cancel();
 Npc->EnvironmentInteraction->RequestAssistance(false);
 Npc->bInteractionPreviewControlled=false;
 Npc->ClearActionAnimationPreview();
 Npc->GetNavigator()->ClearPath();

 FVector Exit=FVector::ZeroVector;
 const int32 Frame=Npc->GetBinaryManager()?Npc->GetBinaryManager()->GetCurrentFrame():0;
 auto* Level=Npc->GetLevelDataManager();
 bool bSafe=Level && Level->TryGetNearestSafeExit(Npc->GetActorLocation(),Frame,Exit);
 if(bSafe)
 {
  auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Npc->GetActorLocation(),Exit,Npc);
  bSafe=Path && Path->IsValid() && !Path->IsPartial();
  if(bSafe) for(const FVector& Point:Path->PathPoints)
   if(Level->IsLocationDangerous(Point,Frame)) { bSafe=false; break; }
 }
 Npc->GetIntentComponent()->ResumeEvacuationAfterInteraction(bSafe);
 Npc->GetBehaviorStateMachine()->ApplyIntentProjection(Npc->GetIntentComponent()->GetCurrentIntent());
 Npc->GetHumanBehaviorSelector()->RequestReselection(TEXT("InteractionEndedReturnToEvacuation"));
 // No teleport, hide, destroy or fake exit success. Normal policy/navigation and
 // SimulationController keep responsibility for the entire remaining evacuation.
 UE_LOG(LogTemp,Display,TEXT("[InteractionPreview] RELEASE %s pos=%s safeRoute=%d exit=%s previewControlled=%d."),
  *Npc->GetName(),*Npc->GetActorLocation().ToString(),bSafe,*Exit.ToString(),Npc->bInteractionPreviewControlled);
}
void AYUFSInteractionPreview::ReleaseResidentsToEvacuation()
{
 ReleaseToEvacuation(User);
 ReleaseToEvacuation(Recipient);
 Stage=EYUFSInteractionPreviewStage::Evacuating;
 Elapsed=0;
 MonitorTimer=0;
}
void AYUFSInteractionPreview::MonitorEvacuation(float Dt)
{
 MonitorTimer-=Dt;
 if(MonitorTimer>0) return;
 MonitorTimer=5.f;
 for(auto* Npc:{User.Get(),Recipient.Get()})
  UE_LOG(LogTemp,Display,TEXT("[InteractionPreview] EVAC %s pos=%s speed=%.1f hidden=%d intent=%d action=%d pathPoints=%d doorPresent=%d open=%d."),
   *Npc->GetName(),*Npc->GetActorLocation().ToString(),Npc->GetVelocity().Size2D(),Npc->IsHidden(),
   int32(Npc->GetCurrentIntent()),int32(Npc->GetLastAction()),Npc->GetNavigator()->GetCurrentPathPoints().Num(),IsValid(Door),Door && Door->IsOpen());
 if(User->IsHidden() && Recipient->IsHidden())
 {
  Stage=EYUFSInteractionPreviewStage::Finished;
  UE_LOG(LogTemp,Display,TEXT("[InteractionPreview] Both residents resolved by simulation controller; door remains in world."));
 }
 else if(Elapsed>180.f)
 {
  Stage=EYUFSInteractionPreviewStage::Finished;
  UE_LOG(LogTemp,Warning,TEXT("[InteractionPreview] Evacuation monitoring timed out; remaining actors keep normal AI. No success or hiding forced."));
 }
}
void AYUFSInteractionPreview::Tick(float Dt)
{
 Super::Tick(Dt);
 if(!IsReady() || !bSimulationStarted || bSimulationPaused) return;
 if(Stage==EYUFSInteractionPreviewStage::Finished) return;
 Elapsed+=Dt;
 if(Stage==EYUFSInteractionPreviewStage::Evacuating) { MonitorEvacuation(Dt); return; }
 const auto& Feedback=User->GetTeamIntegrationComponent()->GetInteractionFeedback();
 if(Stage==EYUFSInteractionPreviewStage::Door && Door)
 {
  if(Door->IsOpen() && User->GetActorLocation().Y>185)
  {
   UE_LOG(LogTemp,Display,TEXT("[InteractionPreview] Door passage complete; continuing to nearby recipient without relocation."));
   StartHelp();
   return;
  }
 }
 if(Stage==EYUFSInteractionPreviewStage::Help && Recipient)
 {
  if(User->GetTeamIntegrationComponent()->GetFeedbackGeneration()>StartingFeedbackGeneration
     && Feedback.RequestRevision==User->GetTeamIntegrationComponent()->GetInteractionDirective().Revision
     && Feedback.Status==EYUFSTeamRequestStatus::Completed && Feedback.Reason==FName(TEXT("PersonGuidedToEvacuate")) && Elapsed>1)
  {
   ReleaseResidentsToEvacuation();
   UE_LOG(LogTemp,Display,TEXT("[InteractionPreview] Help complete; BOTH actors continue evacuation from their current positions."));
   return;
  }
 }
 if(Elapsed>35)
 {
  UE_LOG(LogTemp,Warning,TEXT("[InteractionPreview] Stage %d timed out: %s. Release preview control instead of leaving residents frozen."),int32(Stage),*Feedback.Reason.ToString());
  ReleaseResidentsToEvacuation();
 }
}
