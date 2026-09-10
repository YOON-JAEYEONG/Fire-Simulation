#pragma once

#include "CoreMinimal.h"
#include "Simulation/YUFSSimulationController.h"
#include "YUFSResultEntryObject.generated.h"

/**
 * UMG ListView는 UObject 항목만 받을 수 있어, USTRUCT인 FSimRunResult를 감싸는 래퍼입니다.
 * UYUFSResultsWidget::GetResultEntries()가 생성하고, WBP_ResultRow(YUFSResultRowWidget)가
 * IUserObjectListEntry를 통해 받습니다.
 */
UCLASS(BlueprintType)
class YUFS_API UYUFSResultEntryObject : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "YUFS|Results")
	FSimRunResult RunResult;
};
