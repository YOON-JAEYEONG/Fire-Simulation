// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_FindPanicLocation.generated.h"

UCLASS()
class YUFS_API UBTTask_FindPanicLocation : public UBTTaskNode
{
    GENERATED_BODY()

public:
    UBTTask_FindPanicLocation();

protected:
    virtual EBTNodeResult::Type ExecuteTask(
        UBehaviorTreeComponent& OwnerComp,
        uint8* NodeMemory
    ) override;

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panic")
    float PanicMoveRadius = 1000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blackboard")
    FName TargetLocationKeyName = TEXT("TargetLocation");
};