#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "YUFSMapDiagnostics.generated.h"

class ACharacter;

USTRUCT(BlueprintType)
struct FYUFSMapLocationProbe
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) bool bSupported = false;
    UPROPERTY(BlueprintReadOnly) bool bBodyClear = false;
    UPROPERTY(BlueprintReadOnly) bool bOnNavigation = false;
    UPROPERTY(BlueprintReadOnly) FVector FloorLocation = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) FVector NavigationLocation = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) FString FloorActor;
    UPROPERTY(BlueprintReadOnly) FString Reason;
};

USTRUCT(BlueprintType)
struct FYUFSMapRouteProbe
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) bool bComplete = false;
    UPROPERTY(BlueprintReadOnly) TArray<FVector> FloorPoints;
};

/** Read-only authoring diagnostics. Never relocates actors or certifies fire alignment. */
UCLASS()
class YUFS_API UYUFSMapDiagnostics : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="YUFS|Map|Diagnostics", meta=(WorldContext="WorldContextObject", DevelopmentOnly))
    static FYUFSMapLocationProbe ProbeLocation(UObject* WorldContextObject, FVector ApproximateFeet,
        TSubclassOf<ACharacter> CharacterClass, float VerticalSearchCm = 75.f);

    UFUNCTION(BlueprintCallable, Category="YUFS|Map|Diagnostics", meta=(WorldContext="WorldContextObject", DevelopmentOnly))
    static FYUFSMapRouteProbe ProbeRoute(UObject* WorldContextObject, FVector StartFeet, FVector EndFeet);
};
