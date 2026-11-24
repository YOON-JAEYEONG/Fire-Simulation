#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPC/Integration/YUFSTeamIntegrationTypes.h"
#include "YUFSNpcEnvironmentInteraction.generated.h"
class AYUFSEvacuationNPC;
class AYUFSInteractionDoor;
/** Local executor. Route and animation remain owned by the character/team drivers. */
UCLASS(ClassGroup=(YUFS), meta=(BlueprintSpawnableComponent))
class YUFS_API UYUFSNpcEnvironmentInteraction : public UActorComponent
{
 GENERATED_BODY()
 friend struct FYUFSDoorQueueTestAccess;
public:
 UYUFSNpcEnvironmentInteraction();
 void Observe(float Dt);
 /** Clear a reserved door's physical travel even if this NPC is not itself evacuating yet. */
 bool YieldDoorwaySpace(float Dt, int32 SimFrame);
 bool Execute(float Dt, int32 SimFrame);
 void Cancel();
 bool IsActive() const { return bActive; }
 bool IsOperatingDoor() const { return bActive && ActiveGoal==EYUFSInteractionGoal::OpenDoor; }
 bool IsPassingDoor() const { return PassageDoor.IsValid(); }
 bool IsYieldingDoorSpace() const { return bYieldingDoorSpace; }
 bool NeedsMovement() const { return bActive && bApproaching; }
 FVector GetTarget() const;
 /** A reservation may exist while the helper is still approaching. */
 UFUNCTION(BlueprintPure, Category="NPC|Help") bool IsReceivingAssistance() const;
 /** Hold the recipient only once the reserved helper is within conversational reach. */
 UFUNCTION(BlueprintPure, Category="NPC|Help") bool IsReceivingContactAssistance() const;
 UFUNCTION(BlueprintPure, Category="NPC|Help") AYUFSEvacuationNPC* GetAssistingNPC() const;
 UFUNCTION(BlueprintCallable, Category="NPC|Help") void RequestAssistance(bool bRequested) { bNeedsAssistance=bRequested; }
 UFUNCTION(BlueprintPure, Category="NPC|Help") bool CanReceiveAssistance() const { return NeedsHelp() && !Helper.IsValid(); }
 UFUNCTION(BlueprintCallable, Category="NPC|Help") bool TryReserveHelper(AYUFSEvacuationNPC* Candidate);
 UFUNCTION(BlueprintCallable, Category="NPC|Help") void ReleaseHelper(AYUFSEvacuationNPC* Candidate);
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPC|Interaction") bool bUseExternalExecutor=false;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPC|Help") bool bNeedsAssistance=false;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPC|Help") bool bAcceptsAssistance=true;
 UPROPERTY(EditAnywhere, Category="NPC|Interaction") float SearchRadius=800.f;
 UPROPERTY(EditAnywhere, Category="NPC|Door", meta=(ClampMin="0.0")) float DoorReachSeconds=.35f;
 UPROPERTY(EditAnywhere, Category="NPC|Door", meta=(ClampMin="1.0",ClampMax="45.0")) float DoorFacingToleranceDegrees=15.f;
protected:
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
 bool Visible(AActor* Target) const;
 bool WantsThisDoor(const AYUFSInteractionDoor& AtDoor) const;
 bool IsFrontOfDoorQueue(AYUFSInteractionDoor& AtDoor) const;
 bool ContinueFirstPassage(float Dt, int32 Frame);
 bool NeedsHelp() const;
 bool ClearDoorSwing(int32 Frame, float Dt);
 enum class EDoorStep : uint8 { Operator, ClearLeaf, MakeRoom };
 bool StepAside(const AYUFSInteractionDoor& AtDoor, EDoorStep Step, int32 Frame, float Dt,
     TOptional<FVector>& Target, float& NextSearch);
 void Finish(bool Success, FName Reason);
 void UpdateLocalPose(const FVector& FacingTarget, EYUFSAction Action, float Dt);
 TWeakObjectPtr<AYUFSEvacuationNPC> Npc, Person, Helper;
 TWeakObjectPtr<AYUFSInteractionDoor> Door;
 TWeakObjectPtr<AYUFSInteractionDoor> PassageDoor;
 TOptional<FVector> DoorClearanceTarget;
 TOptional<FVector> YieldClearanceTarget;
 TWeakObjectPtr<AYUFSInteractionDoor> YieldDoor;
 float NextDoorClearanceSearch=0.f;
 float NextYieldSearch=0.f, NextYieldScan=0.f, DoorNoProgress=0.f, LastDoorFraction=0.f;
 FVector LastDoorProgressPosition=FVector::ZeroVector;
 float Scan=0.f, Elapsed=0.f, Contact=0.f, RetryAt=0.f;
 int64 RequestRevision=0;
 EYUFSInteractionGoal ActiveGoal=EYUFSInteractionGoal::None;
 FName ActiveTargetId=NAME_None;
 bool bActive=false, bApproaching=false, bYieldingDoorSpace=false;
};
