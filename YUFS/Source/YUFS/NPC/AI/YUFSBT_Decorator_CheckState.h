#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "Core/YUFSTypes.h"
#include "YUFSBT_Decorator_CheckState.generated.h"

UCLASS()
class YUFS_API UYUFSBT_Decorator_CheckState : public UBTDecorator
{
	GENERATED_BODY()

public:
	UYUFSBT_Decorator_CheckState();

	UPROPERTY(EditAnywhere, Category="Condition")
	TArray<EYUFSBehaviorState> AllowedStates;

protected:
	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual FString GetStaticDescription() const override;
	virtual uint16 GetInstanceMemorySize() const override;
};
