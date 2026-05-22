#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "YUFSBT_Task_Milling.generated.h"

// Milling 상태 전담 Task:
// van der Wal 2021 — 경보+사전녹음없음 → 28% Film, 사회 동조 → AlertOccupants, 기본 → SeekInformation
// 내부 홀드 타이머로 프레임마다 재추첨하지 않음
UCLASS()
class YUFS_API UYUFSBT_Task_Milling : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UYUFSBT_Task_Milling();

	// 한 번 선택한 행동을 유지할 최소 시간 (초)
	UPROPERTY(EditAnywhere, Category="Milling")
	float ActionHoldDuration = 2.5f;

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual uint16 GetInstanceMemorySize() const override;

private:
	enum class EMillingAction : uint8 { SeekInfo, Film, AlertOccupants };

	struct FMillingMemory
	{
		EMillingAction CurrentAction = EMillingAction::SeekInfo;
		float HoldTimer = 0.f;
		float LookAnchorYaw = 0.f;
		float LookElapsed   = 0.f;
	};

	EMillingAction PickAction(UBehaviorTreeComponent& OwnerComp) const;
};
