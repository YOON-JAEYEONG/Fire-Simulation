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

	/** Placed in the level by a designer (runtime bags are spawned with their NPC as Owner). */
	bool IsLevelPlaced() const { return GetOwner() == nullptr; }
	bool IsClaimed() const { return OwnerNpc.IsValid(); }
	/** Level-placed bag: becomes this NPC's left-behind bag. Fails if already claimed or carried. */
	bool Claim(AActor* InOwner);
	/** Level-placed bag: drop it back on the designer's spot and forget the owner (episode reset). */
	void ReleaseToHome();
	/** Level-placed bag carried out of the building: it left with its owner, so hide it for this run. */
	void LeaveWithCarrier();

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
	FTransform HomeTransform;
	bool bHomeCaptured = false;
};
