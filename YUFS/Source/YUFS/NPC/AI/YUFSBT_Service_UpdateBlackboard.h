#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "YUFSBT_Service_UpdateBlackboard.generated.h"

UCLASS()
class YUFS_API UYUFSBT_Service_UpdateBlackboard : public UBTService
{
	GENERATED_BODY()

public:
	UYUFSBT_Service_UpdateBlackboard();

protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
};
