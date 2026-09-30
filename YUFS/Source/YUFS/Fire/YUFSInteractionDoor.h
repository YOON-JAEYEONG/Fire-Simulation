#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "YUFSInteractionDoor.generated.h"
class UStaticMeshComponent;
class USceneComponent;
class UBoxComponent;
class AYUFSSimulationController;
/** Place at a real doorway: origin at the floor hinge, closed leaf along local +Y. */
UCLASS()
class YUFS_API AYUFSInteractionDoor : public AActor
{
 GENERATED_BODY()
public:
 AYUFSInteractionDoor();
 virtual void OnConstruction(const FTransform& Transform) override;
 virtual void Tick(float DeltaSeconds) override;
 /** Reserve without moving the leaf: the executor can stop and face the handle first. */
 UFUNCTION(BlueprintCallable) bool TryReserve(AActor* User);
 UFUNCTION(BlueprintCallable) bool TryUse(AActor* User);
 UFUNCTION(BlueprintCallable) void Release(AActor* User);
 UFUNCTION(BlueprintPure) bool IsOpen() const { return OpenFraction >= 1.f; }
 UFUNCTION(BlueprintPure) bool CanOperate() const { return !bLocked && !bHot && !IsOpen() && FMath::Abs(OpenAngle) >= 75.f; }
 UFUNCTION(BlueprintPure) bool IsReservedBy(AActor* User) const { return Operator.IsValid() && Operator.Get()==User; }
 UFUNCTION(BlueprintPure) bool IsOpeningBlocked() const { return bOpeningBlocked; }
 UFUNCTION(BlueprintPure) bool IsPassageClear() const;
 UFUNCTION(BlueprintPure) bool IsUserInReach(AActor* User) const;
 UFUNCTION(BlueprintPure) FVector GetHandleLocation() const;
 /** Timeline: how far the leaf is open (0..1) and which way it swings (+1/-1). */
 float GetOpenFraction() const { return OpenFraction; }
 float GetSwingDirection() const { return SwingDirection; }
 /** Timeline replay: show the leaf as it was at that moment. No operator, no sweep test. */
 void ApplyReviewState(float InOpenFraction, float InSwingDirection);
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Panel;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door") bool bLocked = false;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door") bool bHot = false;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door") float OpenAngle = 90.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door", meta=(ClampMin="0.1")) float OpenSeconds = 1.2f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door") float UseDistance = 180.f;
 /**
  * Sliding leaf (classroom style): the leaf runs along the wall instead of swinging, so an open
  * door never sticks out into the corridor and nobody has to keep clear of a swing arc.
  */
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door|Sliding") bool bSliding = false;
 /** Sliding: track offset from the doorway plane along local X (cm), e.g. just off the room-side wall face. */
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door|Sliding") float SlideTrackOffset = 0.f;
 /** Sliding: travel along local Y (cm); negative slides toward the hinge-side jamb. */
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door|Sliding") float SlideDistance = -100.f;
private:
 /** Leaf pose for the current OpenFraction: swung about the hinge, or slid along its track. */
 void ApplyLeafPose();
 UPROPERTY(VisibleAnywhere, Category="Door") TObjectPtr<USceneComponent> LeafPivot;
 UPROPERTY(VisibleAnywhere, Category="Door") TObjectPtr<UBoxComponent> PassageBlocker;
 UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> FrameMeshes;
 UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> HardwareMeshes;
 TWeakObjectPtr<AActor> Operator;
 TWeakObjectPtr<AYUFSSimulationController> SimulationController;
 float OpenFraction = 0.f;
 float SwingDirection = 1.f;
 bool bOpeningRequested = false;
 bool bOpeningBlocked = false;
 bool CanSweepLeaf(float FromFraction, float ToFraction) const;
};
