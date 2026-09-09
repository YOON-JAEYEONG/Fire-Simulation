#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "YUFSInteractionDoor.generated.h"
class UStaticMeshComponent;
class USceneComponent;
class UBoxComponent;
/** Place at a real doorway: origin at the floor hinge, closed leaf along local +Y. */
UCLASS()
class YUFS_API AYUFSInteractionDoor : public AActor
{
 GENERATED_BODY()
public:
 AYUFSInteractionDoor();
 virtual void OnConstruction(const FTransform& Transform) override;
 virtual void Tick(float DeltaSeconds) override;
 UFUNCTION(BlueprintCallable) bool TryUse(AActor* User);
 UFUNCTION(BlueprintCallable) void Release(AActor* User);
 UFUNCTION(BlueprintPure) bool IsOpen() const { return OpenFraction >= 1.f; }
 UFUNCTION(BlueprintPure) bool CanOperate() const { return !bLocked && !bHot && !IsOpen(); }
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Panel;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door") bool bLocked = false;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door") bool bHot = false;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door") float OpenAngle = 90.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door", meta=(ClampMin="0.1")) float OpenSeconds = 1.2f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door") float UseDistance = 180.f;
private:
 UPROPERTY(VisibleAnywhere, Category="Door") TObjectPtr<USceneComponent> LeafPivot;
 UPROPERTY(VisibleAnywhere, Category="Door") TObjectPtr<UBoxComponent> PassageBlocker;
 UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> FrameMeshes;
 UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> HardwareMeshes;
 TWeakObjectPtr<AActor> Operator;
 float OpenFraction = 0.f;
 float SwingDirection = 1.f;
};
