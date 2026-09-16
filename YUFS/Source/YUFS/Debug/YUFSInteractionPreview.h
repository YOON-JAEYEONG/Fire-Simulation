#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "YUFSInteractionPreview.generated.h"
class AYUFSEvacuationNPC;
class AYUFSInteractionDoor;
enum class EYUFSInteractionPreviewStage : uint8 { Waiting, Door, Help, Evacuating, Finished };
/** World-only interaction fixture. Never owns UI, player input, or the view target. */
UCLASS()
class YUFS_API AYUFSInteractionPreview : public AActor
{
 GENERATED_BODY()
public:
 AYUFSInteractionPreview();
 virtual void BeginPlay() override;
 virtual void Tick(float Dt) override;
 bool IsReady() const;
 void StartSequence();
 void SetSimulationPaused(bool bPaused);
private:
 friend class FYUFSInteractionContinuityTest;
 bool Place(AYUFSEvacuationNPC* Npc,const FVector& FloorPoint);
 void PrepareResidentsOnce();
 void StartHelp();
 void ReleaseResidentsToEvacuation();
 void ReleaseToEvacuation(AYUFSEvacuationNPC* Npc);
 void MonitorEvacuation(float Dt);
 UPROPERTY() TObjectPtr<AYUFSEvacuationNPC> User;
 UPROPERTY() TObjectPtr<AYUFSEvacuationNPC> Recipient;
 UPROPERTY() TObjectPtr<AYUFSInteractionDoor> Door;
 EYUFSInteractionPreviewStage Stage=EYUFSInteractionPreviewStage::Waiting;
 float Elapsed=0;
 bool bSimulationStarted=false;
 bool bSimulationPaused=false;
 float MonitorTimer=0;
 int64 StartingFeedbackGeneration=0;
};
