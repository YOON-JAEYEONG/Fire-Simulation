#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "YUFSBT_Task_PlayMontage.generated.h"

// Film / Cough 등 몽타주 재생 Task
UCLASS()
class YUFS_API UYUFSBT_Task_PlayMontage : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UYUFSBT_Task_PlayMontage();

	UPROPERTY(EditAnywhere, Category="Montage")
	bool bUseCoughMontage = false; // false = Film, true = Cough

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};
