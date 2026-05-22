#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "YUFSBT_Task_EvacuateToExit.generated.h"

UENUM()
enum class EYUFSExitChoice : uint8
{
	Nearest,   // 가장 가까운 안전 출구 (스태프 안내 있으면 우선)
	Familiar,  // Affiliative Model — 친숙한 출구
	Crowd,     // 군중 휩쓸리기
};

UCLASS()
class YUFS_API UYUFSBT_Task_EvacuateToExit : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UYUFSBT_Task_EvacuateToExit();

	UPROPERTY(EditAnywhere, Category="Exit")
	EYUFSExitChoice ExitChoice = EYUFSExitChoice::Nearest;

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

private:
	FVector ResolveTarget(UBehaviorTreeComponent& OwnerComp) const;
};
