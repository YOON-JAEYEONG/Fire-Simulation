#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "YUFSBT_Task_SeekInformation.generated.h"

UCLASS()
class YUFS_API UYUFSBT_Task_SeekInformation : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UYUFSBT_Task_SeekInformation();

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual uint16 GetInstanceMemorySize() const override;
};
