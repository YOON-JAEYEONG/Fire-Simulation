#include "NPC/AI/YUFSBT_Task_EvacuateToExit.h"
#include "NPC/AI/YUFSNPCAIController.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "NPC/Social/YUFSSocialInfluenceComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

UYUFSBT_Task_EvacuateToExit::UYUFSBT_Task_EvacuateToExit()
{
	NodeName = TEXT("Evacuate To Exit");
	bNotifyTick = true;
}

EBTNodeResult::Type UYUFSBT_Task_EvacuateToExit::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AYUFSNPCAIController* Ctrl = Cast<AYUFSNPCAIController>(OwnerComp.GetAIOwner());
	AYUFSEvacuationNPC*   NPC  = Ctrl ? Cast<AYUFSEvacuationNPC>(Ctrl->GetPawn()) : nullptr;
	if (!NPC) return EBTNodeResult::Failed;

	// MaxWalkSpeed 복원 (일시정지 시 0이 됐을 경우)
	if (UCharacterMovementComponent* Mv = NPC->GetCharacterMovement())
		if (Mv->MaxWalkSpeed < 1.f)
			Mv->MaxWalkSpeed = 400.f;

	const FVector Target = ResolveTarget(OwnerComp);
	if (Target.IsZero()) return EBTNodeResult::Failed;

	if (UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent())
		BB->SetValueAsVector(YUFSBlackboardKeys::TargetLocation, Target);

	// 첫 경로 요청 — 이미 pathfinding 중이면 스킵
	if (UYUFSSmokeAwareNavigator* Nav = NPC->GetNavigator())
	{
		if (!Nav->bIsPathfinding)
		{
			Nav->ClearPath();
			Nav->RequestPathAsync(Target, NPC->GetCurrentSimFramePublic());
		}
	}

	return EBTNodeResult::InProgress;
}

void UYUFSBT_Task_EvacuateToExit::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	AYUFSNPCAIController* Ctrl = Cast<AYUFSNPCAIController>(OwnerComp.GetAIOwner());
	AYUFSEvacuationNPC*   NPC  = Ctrl ? Cast<AYUFSEvacuationNPC>(Ctrl->GetPawn()) : nullptr;
	if (!NPC) { FinishLatentTask(OwnerComp, EBTNodeResult::Failed); return; }

	UYUFSSmokeAwareNavigator* Nav = NPC->GetNavigator();
	if (!Nav) { FinishLatentTask(OwnerComp, EBTNodeResult::Failed); return; }

	const FVector Target = ResolveTarget(OwnerComp);
	if (Target.IsZero()) { FinishLatentTask(OwnerComp, EBTNodeResult::Failed); return; }

	// 연기 우회 재탐색
	Nav->CheckAndReroute(NPC->GetCurrentSimFramePublic());

	// 목적지가 500cm 이상 바뀌었을 때만 경로 재요청 (pathfinding 중엔 스킵)
	const FVector CurrentDest = Nav->GetCurrentDestination();
	if (!Nav->bIsPathfinding && FVector::Dist(Target, CurrentDest) > 500.f)
	{
		if (UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent())
			BB->SetValueAsVector(YUFSBlackboardKeys::TargetLocation, Target);
		Nav->ClearPath();
		Nav->RequestPathAsync(Target, NPC->GetCurrentSimFramePublic());
	}

	// 경로가 아직 계산 중이면 이동 입력 보류 — 진동 방지
	if (Nav->bIsPathfinding || Nav->GetCurrentPathPoints().IsEmpty())
		return;

	NPC->DriveMovementToward(Target);

	// 출구 도달 판정
	if (FVector::Dist2D(NPC->GetActorLocation(), Target) < 150.f)
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
}

EBTNodeResult::Type UYUFSBT_Task_EvacuateToExit::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	if (AYUFSNPCAIController* Ctrl = Cast<AYUFSNPCAIController>(OwnerComp.GetAIOwner()))
		if (AYUFSEvacuationNPC* NPC = Cast<AYUFSEvacuationNPC>(Ctrl->GetPawn()))
			if (UYUFSSmokeAwareNavigator* Nav = NPC->GetNavigator())
				Nav->ClearPath();
	return EBTNodeResult::Aborted;
}

FVector UYUFSBT_Task_EvacuateToExit::ResolveTarget(UBehaviorTreeComponent& OwnerComp) const
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) return FVector::ZeroVector;

	AYUFSNPCAIController* Ctrl = Cast<AYUFSNPCAIController>(OwnerComp.GetAIOwner());
	AYUFSEvacuationNPC*   NPC  = Ctrl ? Cast<AYUFSEvacuationNPC>(Ctrl->GetPawn()) : nullptr;

	switch (ExitChoice)
	{
	case EYUFSExitChoice::Nearest:
		if (NPC && NPC->HasReceivedStaffGuidance())
		{
			const FVector Staff = BB->GetValueAsVector(YUFSBlackboardKeys::StaffExitLoc);
			if (!Staff.IsZero()) return Staff;
		}
		return BB->GetValueAsVector(YUFSBlackboardKeys::NearestExitLoc);

	case EYUFSExitChoice::Familiar:
		return BB->GetValueAsVector(YUFSBlackboardKeys::FamiliarExitLoc);

	case EYUFSExitChoice::Crowd:
		if (NPC)
			if (UYUFSSocialInfluenceComponent* Social = NPC->GetSocialComponent())
			{
				const FVector Avg = Social->GetAverageEvacuationDestination();
				if (!Avg.IsZero()) return Avg;
			}
		return BB->GetValueAsVector(YUFSBlackboardKeys::NearestExitLoc);
	}
	return FVector::ZeroVector;
}
