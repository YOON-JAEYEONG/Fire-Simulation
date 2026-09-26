#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "YUFSFireAssetLibrary.generated.h"

class UMaterialInstanceConstant;

/** Editor helpers for fire asset setup scripts (Content/Python/import_it_test_fire_data.py). */
UCLASS()
class YUFS_API UYUFSFireAssetLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Override a static component mask parameter (e.g. "Density Mask") on a material instance
	 * and rebuild its static permutation. Editor only; returns false in a cooked game.
	 */
	UFUNCTION(BlueprintCallable, Category="YUFS|Fire|Editor")
	static bool SetStaticComponentMaskParameter(UMaterialInstanceConstant* Instance, FName ParameterName,
		bool bR, bool bG, bool bB, bool bA);
};
