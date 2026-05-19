// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_SelectExit.generated.h"

UCLASS()
class YUFS_API UBTTask_SelectExit : public UBTTaskNode
{
    GENERATED_BODY()

public:
    UBTTask_SelectExit();

protected:
    virtual EBTNodeResult::Type ExecuteTask(
        UBehaviorTreeComponent& OwnerComp,
        uint8* NodeMemory
    ) override;

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blackboard")
    FName TargetLocationKeyName = TEXT("TargetLocation");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blackboard")
    FName ExitSelectModeKeyName = TEXT("ExitSelectMode");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exit")
    FName ExitTag = TEXT("Exit");
};
