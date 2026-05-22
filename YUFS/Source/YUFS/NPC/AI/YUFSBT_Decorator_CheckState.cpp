#include "NPC/AI/YUFSBT_Decorator_CheckState.h"
#include "NPC/AI/YUFSNPCAIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"

struct FCheckStateMemory
{
	int32 LastKnownState = -1;
};

UYUFSBT_Decorator_CheckState::UYUFSBT_Decorator_CheckState()
{
	NodeName = TEXT("Check PADM State");
	bNotifyBecomeRelevant = false;
	bNotifyCeaseRelevant  = false;
	bNotifyTick           = true;
	// 틱 간격 — 매 프레임 체크는 과하므로 0.1s 주기로
	FlowAbortMode = EBTFlowAbortMode::Both;
}

bool UYUFSBT_Decorator_CheckState::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	const UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB || AllowedStates.IsEmpty()) return false;

	const EYUFSBehaviorState Current = static_cast<EYUFSBehaviorState>(
		BB->GetValueAsInt(YUFSBlackboardKeys::BehaviorState));

	return AllowedStates.Contains(Current);
}

void UYUFSBT_Decorator_CheckState::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	const UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) return;

	FCheckStateMemory* Mem = reinterpret_cast<FCheckStateMemory*>(NodeMemory);
	const int32 Current = BB->GetValueAsInt(YUFSBlackboardKeys::BehaviorState);

	// 상태가 바뀐 경우에만 BT 재평가 요청 — 매 틱 RequestExecution은 오버헤드
	if (Current != Mem->LastKnownState)
	{
		Mem->LastKnownState = Current;
		OwnerComp.RequestExecution(this);
	}
}

uint16 UYUFSBT_Decorator_CheckState::GetInstanceMemorySize() const
{
	return sizeof(FCheckStateMemory);
}

FString UYUFSBT_Decorator_CheckState::GetStaticDescription() const
{
	FString Desc = TEXT("State in [");
	const UEnum* Enum = StaticEnum<EYUFSBehaviorState>();
	for (int32 i = 0; i < AllowedStates.Num(); ++i)
	{
		if (i > 0) Desc += TEXT(", ");
		Desc += Enum ? Enum->GetNameStringByValue(static_cast<int64>(AllowedStates[i])) : TEXT("?");
	}
	Desc += TEXT("]");
	return Desc;
}
