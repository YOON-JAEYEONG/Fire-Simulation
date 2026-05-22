#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "YUFSBT_Task_MoveToShelter.generated.h"

UCLASS()
class YUFS_API UYUFSBT_Task_MoveToShelter : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UYUFSBT_Task_MoveToShelter();

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};
