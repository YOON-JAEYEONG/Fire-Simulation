// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BTService_UpdateSmokeAwareness.generated.h"

UCLASS()
class YUFS_API UBTService_UpdateSmokeAwareness : public UBTService
{
    GENERATED_BODY()

public:
    UBTService_UpdateSmokeAwareness();

protected:
    virtual void TickNode(
        UBehaviorTreeComponent& OwnerComp,
        uint8* NodeMemory,
        float DeltaSeconds
    ) override;
};
