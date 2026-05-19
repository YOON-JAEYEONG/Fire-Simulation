// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_FindNearestExit.generated.h"

UCLASS()
class YUFS_API UBTTask_FindNearestExit : public UBTTaskNode
{
    GENERATED_BODY()

public:
    UBTTask_FindNearestExit();

protected:
    virtual EBTNodeResult::Type ExecuteTask(
        UBehaviorTreeComponent& OwnerComp,
        uint8* NodeMemory
    ) override;
};