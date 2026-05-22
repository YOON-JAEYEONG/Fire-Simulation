#include "NPC/AI/YUFSBT_Task_PlayMontage.h"
#include "NPC/AI/YUFSNPCAIController.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"

UYUFSBT_Task_PlayMontage::UYUFSBT_Task_PlayMontage()
{
	NodeName = TEXT("Play Montage");
	bNotifyTick = false;
}

EBTNodeResult::Type UYUFSBT_Task_PlayMontage::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AYUFSNPCAIController* Controller = Cast<AYUFSNPCAIController>(OwnerComp.GetAIOwner());
	if (!Controller) return EBTNodeResult::Failed;

	AYUFSEvacuationNPC* NPC = Cast<AYUFSEvacuationNPC>(Controller->GetPawn());
	if (!NPC) return EBTNodeResult::Failed;

	UAnimMontage* Montage = bUseCoughMontage ? NPC->CoughMontage : NPC->FilmMontage;
	if (!Montage) return EBTNodeResult::Succeeded; // 몽타주 없으면 무시

	UAnimInstance* Anim = NPC->GetMesh() ? NPC->GetMesh()->GetAnimInstance() : nullptr;
	if (Anim && !Anim->Montage_IsPlaying(Montage))
	{
		NPC->PlayAnimMontage(Montage);
	}

	return EBTNodeResult::Succeeded;
}
