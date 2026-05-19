// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_FindInjuredNPC.generated.h"

UCLASS()
class YUFS_API UBTTask_FindInjuredNPC : public UBTTaskNode
{
    GENERATED_BODY()

public:
    UBTTask_FindInjuredNPC();

protected:
    virtual EBTNodeResult::Type ExecuteTask(
        UBehaviorTreeComponent& OwnerComp,
        uint8* NodeMemory
    ) override;

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Help")
    float SearchRadius = 1000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blackboard")
    FName TargetNPCKeyName = TEXT("TargetNPC");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blackboard")
    FName TargetLocationKeyName = TEXT("TargetLocation");
};