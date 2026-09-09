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
public:
 UYUFSNpcEnvironmentInteraction();
 void Observe(float Dt);
 bool Execute(float Dt, int32 SimFrame);
 void Cancel();
 bool IsActive() const { return bActive; }
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
protected:
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
 bool Visible(AActor* Target) const;
 bool NeedsHelp() const;
 void Finish(bool Success, FName Reason);
 void UpdateLocalPose(const FVector& FacingTarget, EYUFSAction Action, float Dt);
 TWeakObjectPtr<AYUFSEvacuationNPC> Npc, Person, Helper;
 TWeakObjectPtr<AYUFSInteractionDoor> Door;
 float Scan=0.f, Elapsed=0.f, Contact=0.f, RetryAt=0.f;
 int64 RequestRevision=0;
 EYUFSInteractionGoal ActiveGoal=EYUFSInteractionGoal::None;
 FName ActiveTargetId=NAME_None;
 bool bActive=false, bApproaching=false;
};
