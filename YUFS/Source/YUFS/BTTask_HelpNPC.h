// Fill out your copyright notice in the Description page of Project Settings.
#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_HelpNPC.generated.h"

UCLASS()
class YUFS_API UBTTask_HelpNPC : public UBTTaskNode
{
    GENERATED_BODY()

public:
    UBTTask_HelpNPC();

protected:
    virtual EBTNodeResult::Type ExecuteTask(
        UBehaviorTreeComponent& OwnerComp,
        uint8* NodeMemory
    ) override;

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blackboard")
    FName TargetNPCKeyName = TEXT("TargetNPC");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Help")
    float HelpDistance = 150.0f;
};