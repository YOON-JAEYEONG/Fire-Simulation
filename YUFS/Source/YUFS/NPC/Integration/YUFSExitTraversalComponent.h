#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "YUFSExitTraversalComponent.generated.h"
class AYUFSExitPoint;

/** One physical result shared by the controller, learning and replay. Not an overlap trigger. */
UCLASS(ClassGroup=(YUFS))
class YUFS_API UYUFSExitTraversalComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UYUFSExitTraversalComponent();
	void Reset();
	void DiscardMovementSample();
	void ObserveMovement(const TArray<AYUFSExitPoint*>& Exits, bool bEvacuating, float Dt);
	bool HasExited() const { return bExited; }
	FName GetExitId() const { return ExitId; }
	float GetExitTime() const { return ExitTime; }
	FVector GetExteriorDestination() const { return ExteriorDestination; }
	bool HasExteriorSupport(const FVector& Direction, float LookAheadCm) const;
private:
	TWeakObjectPtr<AYUFSExitPoint> Crossing;
	FVector PreviousPosition = FVector::ZeroVector;
	FVector ExteriorDestination = FVector::ZeroVector;
	bool bHaveSample = false, bExited = false;
	bool bRestoreLedgeFlags = false, bSavedWalkOffLedges = true, bSavedCrouchOffLedges = false;
	FName ExitId;
	float ExitTime = 0.f;
};
