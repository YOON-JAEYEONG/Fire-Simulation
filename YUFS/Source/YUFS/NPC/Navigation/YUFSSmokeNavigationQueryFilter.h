// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "YUFSSmokeNavigationQueryFilter.generated.h"

class AYUFSBinaryManager;
/**
 * 
 */
UCLASS()
class YUFS_API UYUFSSmokeNavigationQueryFilter : public UNavigationQueryFilter
{
	GENERATED_BODY()

public:
// BinaryManager에서 연기 밀도를 읽어 NavArea 비용을 동적으로 갱신
void UpdateSmokeCosts(AYUFSBinaryManager* BinaryManager, int32 CurrentFrame);
};