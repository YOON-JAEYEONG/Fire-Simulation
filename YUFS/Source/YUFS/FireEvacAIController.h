#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "FireEvacAIController.generated.h"

class UBehaviorTree;
class UBlackboardData;

UCLASS()
class YUFS_API AFireEvacAIController : public AAIController
{
    GENERATED_BODY()

public:
    AFireEvacAIController();

protected:
    virtual void BeginPlay() override;
    virtual void OnPossess(APawn* InPawn) override;

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
    UBehaviorTree* BehaviorTreeAsset;
};