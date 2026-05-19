// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_FindRandomWanderLocation.generated.h"

UCLASS()
class YUFS_API UBTTask_FindRandomWanderLocation : public UBTTaskNode
{
    GENERATED_BODY()

public:
    UBTTask_FindRandomWanderLocation();

protected:
    virtual EBTNodeResult::Type ExecuteTask(
        UBehaviorTreeComponent& OwnerComp,
        uint8* NodeMemory
    ) override;

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wander")
    float WanderRadius = 500.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blackboard")
    FName WanderLocationKeyName = TEXT("WanderLocation");
};