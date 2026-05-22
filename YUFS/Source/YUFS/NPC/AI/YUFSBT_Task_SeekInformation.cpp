#include "NPC/AI/YUFSBT_Task_SeekInformation.h"
#include "NPC/AI/YUFSNPCAIController.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Behavior/YUFSBehaviorConfig.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

struct FSeekInfoMemory
{
	float AnchorYaw = 0.f;
	float Elapsed   = 0.f;
};

UYUFSBT_Task_SeekInformation::UYUFSBT_Task_SeekInformation()
{
	NodeName = TEXT("Seek Information (Look Around)");
	bNotifyTick = true;
	bCreateNodeInstance = false;
}

EBTNodeResult::Type UYUFSBT_Task_SeekInformation::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AYUFSNPCAIController* Controller = Cast<AYUFSNPCAIController>(OwnerComp.GetAIOwner());
	if (!Controller) return EBTNodeResult::Failed;

	AYUFSEvacuationNPC* NPC = Cast<AYUFSEvacuationNPC>(Controller->GetPawn());
	if (!NPC) return EBTNodeResult::Failed;

	FSeekInfoMemory* Mem = reinterpret_cast<FSeekInfoMemory*>(NodeMemory);
	Mem->AnchorYaw = NPC->GetActorRotation().Yaw;
	Mem->Elapsed   = 0.f;

	if (NPC->GetCharacterMovement())
		NPC->GetCharacterMovement()->StopMovementImmediately();

	return EBTNodeResult::InProgress;
}

void UYUFSBT_Task_SeekInformation::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	AYUFSNPCAIController* Controller = Cast<AYUFSNPCAIController>(OwnerComp.GetAIOwner());
	if (!Controller) { FinishLatentTask(OwnerComp, EBTNodeResult::Failed); return; }

	AYUFSEvacuationNPC* NPC = Cast<AYUFSEvacuationNPC>(Controller->GetPawn());
	if (!NPC) { FinishLatentTask(OwnerComp, EBTNodeResult::Failed); return; }

	const UYUFSBehaviorStateMachine* SM = NPC->GetBehaviorStateMachine();
	if (!SM || !SM->Config) { FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded); return; }

	FSeekInfoMemory* Mem = reinterpret_cast<FSeekInfoMemory*>(NodeMemory);
	Mem->Elapsed += DeltaSeconds;

	const float Osc = FMath::Sin(Mem->Elapsed * 2.f * PI * SM->Config->LookAroundFrequencyHz);
	FRotator Rot = NPC->GetActorRotation();
	Rot.Yaw = Mem->AnchorYaw + Osc * SM->Config->LookAroundYawAmplitudeDegrees;
	NPC->SetActorRotation(Rot);

	// 상태머신이 다른 상태로 넘어가면 BT가 Decorator로 중단시키므로
	// 이 Task는 무한 InProgress — 중단은 BT 구조가 처리
}

uint16 UYUFSBT_Task_SeekInformation::GetInstanceMemorySize() const
{
	return sizeof(FSeekInfoMemory);
}
