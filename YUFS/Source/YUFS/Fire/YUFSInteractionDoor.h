#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "YUFSInteractionDoor.generated.h"
class UStaticMeshComponent;
class USceneComponent;
class UBoxComponent;
class AYUFSSimulationController;
class ACharacter;
struct FYUFSTimelineDoorSnapshot;

UENUM(BlueprintType)
enum class EYUFSDoorSwingSide : uint8
{
 AwayFromOperator,
 LocalNegativeX,
 LocalPositiveX
};

/** Place at a real doorway: origin at the floor hinge, closed leaf along local +Y. */
UCLASS()
class YUFS_API AYUFSInteractionDoor : public AActor
{
 GENERATED_BODY()
public:
 AYUFSInteractionDoor();
 virtual void OnConstruction(const FTransform& Transform) override;
 virtual void BeginPlay() override;
 virtual void Tick(float DeltaSeconds) override;
 /** Reserve without moving the leaf: the executor can stop and face the handle first. */
 UFUNCTION(BlueprintCallable) bool TryReserve(AActor* User);
 UFUNCTION(BlueprintCallable) bool TryUse(AActor* User);
 UFUNCTION(BlueprintCallable) void Release(AActor* User);
 UFUNCTION(BlueprintPure) bool IsOpen() const { return OpenFraction >= 1.f; }
 UFUNCTION(BlueprintPure) bool CanOperate() const { return !bLocked && !bHot && !IsOpen() && (UsesSlidingMotion() || FMath::Abs(OpenAngle) >= 75.f); }
 UFUNCTION(BlueprintPure) bool UsesSlidingMotion() const;
 UFUNCTION(BlueprintPure) bool IsReservedBy(AActor* User) const { return Operator.IsValid() && Operator.Get()==User; }
 UFUNCTION(BlueprintPure) bool IsOpeningBlocked() const { return bOpeningBlocked; }
 UFUNCTION(BlueprintPure) bool IsPassageClear() const;
 UFUNCTION(BlueprintPure) bool IsUserInReach(AActor* User) const;
 UFUNCTION(BlueprintPure) FVector GetHandleLocation() const;
 UFUNCTION(BlueprintPure) float GetOpenFraction() const { return OpenFraction; }
 bool IsLocationInReach(FVector Location) const;
 /** Stable doorway geometry, independent of whether its leaf is open. Inputs are floor points. */
 bool IntersectPortal(const FVector& From, const FVector& To, FVector& Crossing) const;
 FVector GetPortalLandingOpposite(const FVector& From) const;
 FVector GetPortalFloorPoint(double SideOffsetCm, double WidthFraction=0.5) const;
 bool ConflictsWithOpening(const ACharacter& Walker, FVector From, FVector To) const;
 bool IsInOpeningSwing(const ACharacter& Character, FVector Location) const;
 bool IsInsidePropClearance(FVector FloorLocation, float ExtraClearanceCm) const;
 FYUFSTimelineDoorSnapshot BuildTimelineSnapshot() const;
 void ApplyTimelineSnapshot(const FYUFSTimelineDoorSnapshot& Snapshot);
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UStaticMeshComponent> Panel;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door") bool bLocked = false;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door") bool bHot = false;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door|Sliding") bool bSlidingDoor = false;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door|Sliding") float SlideMountLocalX = 30.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door") float OpenAngle = 90.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door") EYUFSDoorSwingSide SwingSide = EYUFSDoorSwingSide::AwayFromOperator;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door", meta=(ClampMin="0.1")) float OpenSeconds = 1.2f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door") float UseDistance = 180.f;
private:
 UPROPERTY(VisibleAnywhere, Category="Door") TObjectPtr<USceneComponent> LeafPivot;
 UPROPERTY(VisibleAnywhere, Category="Door") TObjectPtr<UBoxComponent> PassageBlocker;
 UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> FrameMeshes;
 UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> HardwareMeshes;
 UPROPERTY() TObjectPtr<UStaticMeshComponent> SlidingRail;
 TWeakObjectPtr<AActor> Operator;
 TWeakObjectPtr<AYUFSSimulationController> SimulationController;
 float OpenFraction = 0.f;
 float SwingDirection = 1.f;
 float ResolvedSlideMount = 0.f;
 bool bSlideTrackResolved = false;
 bool bOpeningRequested = false;
 bool bOpeningBlocked = false;
 bool bTimelinePresentation = false;
 FTransform GetSwingTransform(float Yaw) const;
 float GetSlideMount() const;
 FTransform GetLeafTransform(float Fraction) const;
 void ApplyLeafPose();
 void ResolveSlidingTrack();
 bool IntersectsRemainingSwing(const ACharacter& Character, FVector From, FVector To) const;
 double GetMaximumSwingRadius() const;
 bool CanSweepLeaf(float FromFraction, float ToFraction, bool bReportBlocker, bool bOnlyTrackGeometry=false) const;
};
