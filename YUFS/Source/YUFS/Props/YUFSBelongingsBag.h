#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "YUFSBelongingsBag.generated.h"

class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EYUFSBelongingsState : uint8
{
	/** Left at the owner's original position; not yet collected. */
	LeftBehind,
	/** Picked up and carried on the owner's back. */
	Carried
};

/**
 * A large personal belonging (bag) that one NPC left behind before the fire.
 * Built from engine basic shapes so no extra binary asset is required.
 * It never blocks navigation or pawns: it is a visual target, not an obstacle.
 */
UCLASS()
class YUFS_API AYUFSBelongingsBag : public AActor
{
	GENERATED_BODY()

public:
	AYUFSBelongingsBag();

	void AssignOwnerNpc(AActor* InOwner);
	AActor* GetOwnerNpc() const { return OwnerNpc.Get(); }

	EYUFSBelongingsState GetBelongingsState() const { return State; }
	bool IsCarried() const { return State == EYUFSBelongingsState::Carried; }

	/** Point the NPC walks to (floor-level centre of the bag). */
	FVector GetPickupLocation() const;

	/** Attach to the carrier's back. Only the assigned owner may carry it. */
	bool AttachToCarrier(AActor* Carrier);

	UPROPERTY(VisibleAnywhere, Category="Belongings")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, Category="Belongings")
	TObjectPtr<UStaticMeshComponent> Strap;

	/** Offset from the carrier's root (capsule centre, +X forward) while carried. */
	UPROPERTY(EditAnywhere, Category="Belongings")
	FVector CarryOffset = FVector(-42.f, 0.f, 30.f);

	UPROPERTY(EditAnywhere, Category="Belongings")
	FLinearColor BagColor = FLinearColor(0.08f, 0.16f, 0.45f);

protected:
	virtual void BeginPlay() override;

private:
	TWeakObjectPtr<AActor> OwnerNpc;
	EYUFSBelongingsState State = EYUFSBelongingsState::LeftBehind;
};
