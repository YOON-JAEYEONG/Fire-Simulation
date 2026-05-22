#include "NPC/AI/YUFSBT_Task_Milling.h"
#include "NPC/AI/YUFSNPCAIController.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Behavior/YUFSBehaviorConfig.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "Animation/AnimInstance.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

UYUFSBT_Task_Milling::UYUFSBT_Task_Milling()
{
	NodeName = TEXT("Milling (PADM)");
	bNotifyTick = true;
	bCreateNodeInstance = false;
}

EBTNodeResult::Type UYUFSBT_Task_Milling::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AYUFSNPCAIController* Ctrl = Cast<AYUFSNPCAIController>(OwnerComp.GetAIOwner());
	AYUFSEvacuationNPC*   NPC  = Ctrl ? Cast<AYUFSEvacuationNPC>(Ctrl->GetPawn()) : nullptr;
	if (!NPC) return EBTNodeResult::Failed;

	FMillingMemory* Mem = reinterpret_cast<FMillingMemory*>(NodeMemory);
	Mem->CurrentAction = PickAction(OwnerComp);
	Mem->HoldTimer     = 0.f;
	Mem->LookAnchorYaw = NPC->GetActorRotation().Yaw;
	Mem->LookElapsed   = 0.f;

	if (UCharacterMovementComponent* Mv = NPC->GetCharacterMovement())
		Mv->StopMovementImmediately();

	return EBTNodeResult::InProgress;
}

void UYUFSBT_Task_Milling::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	AYUFSNPCAIController* Ctrl = Cast<AYUFSNPCAIController>(OwnerComp.GetAIOwner());
	AYUFSEvacuationNPC*   NPC  = Ctrl ? Cast<AYUFSEvacuationNPC>(Ctrl->GetPawn()) : nullptr;
	if (!NPC) { FinishLatentTask(OwnerComp, EBTNodeResult::Failed); return; }

	FMillingMemory* Mem = reinterpret_cast<FMillingMemory*>(NodeMemory);
	Mem->HoldTimer += DeltaSeconds;

	// 홀드 시간 초과 시 재선택
	if (Mem->HoldTimer >= ActionHoldDuration)
	{
		Mem->CurrentAction = PickAction(OwnerComp);
		Mem->HoldTimer     = 0.f;
		Mem->LookAnchorYaw = NPC->GetActorRotation().Yaw;
		Mem->LookElapsed   = 0.f;
	}

	const UYUFSBehaviorStateMachine* SM = NPC->GetBehaviorStateMachine();
	const UYUFSBehaviorConfig* Cfg = SM ? SM->Config : nullptr;

	switch (Mem->CurrentAction)
	{
	case EMillingAction::Film:
		if (NPC->FilmMontage)
		{
			UAnimInstance* Anim = NPC->GetMesh() ? NPC->GetMesh()->GetAnimInstance() : nullptr;
			if (Anim && !Anim->Montage_IsPlaying(NPC->FilmMontage))
				NPC->PlayAnimMontage(NPC->FilmMontage);
		}
		break;

	case EMillingAction::AlertOccupants:
		// 이동 없이 두리번 — SeekInformation과 동일 시각적 표현
		// (실제 알림 행동은 사회적 영향 컴포넌트가 주변 NPC 상태로 처리)
		[[fallthrough]];

	case EMillingAction::SeekInfo:
	default:
		if (Cfg)
		{
			Mem->LookElapsed += DeltaSeconds;
			const float Osc = FMath::Sin(Mem->LookElapsed * 2.f * PI * Cfg->LookAroundFrequencyHz);
			FRotator Rot = NPC->GetActorRotation();
			Rot.Yaw = Mem->LookAnchorYaw + Osc * Cfg->LookAroundYawAmplitudeDegrees;
			NPC->SetActorRotation(Rot);
		}
		break;
	}
}

UYUFSBT_Task_Milling::EMillingAction UYUFSBT_Task_Milling::PickAction(UBehaviorTreeComponent& OwnerComp) const
{
	const UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) return EMillingAction::SeekInfo;

	const bool bAlarm        = BB->GetValueAsBool(YUFSBlackboardKeys::bAlarmSounding);
	const bool bPreRecorded  = BB->GetValueAsBool(YUFSBlackboardKeys::bPreRecordedMsg);
	const float EvacRatio    = BB->GetValueAsFloat(YUFSBlackboardKeys::NearbyEvacRatio);

	// van der Wal 2021: 경보O + 사전녹음X → Film OR 3.43 (28% 확률로 표현)
	if (bAlarm && !bPreRecorded && FMath::FRand() < 0.28f)
		return EMillingAction::Film;

	// Latane&Darley: 30% 이상 대피 중 또는 사전녹음 수신 → 정보 전파
	if (EvacRatio > 0.3f || bPreRecorded)
		return EMillingAction::AlertOccupants;

	return EMillingAction::SeekInfo;
}

uint16 UYUFSBT_Task_Milling::GetInstanceMemorySize() const
{
	return sizeof(FMillingMemory);
}
