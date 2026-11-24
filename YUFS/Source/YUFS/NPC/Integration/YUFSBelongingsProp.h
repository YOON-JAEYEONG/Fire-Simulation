#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "YUFSBelongingsProp.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;

UENUM(BlueprintType)
enum class EYUFSBelongingsPropKind : uint8 { Backpack, Notebook };

/** Visible set dressing or the visual for an NPC's private, existing return target. */
UCLASS()
class YUFS_API AYUFSBelongingsProp : public AActor
{
	GENERATED_BODY()
public:
	AYUFSBelongingsProp();
	virtual void OnConstruction(const FTransform& Transform) override;
	void PlaceOnFloor(FVector Floor, float Yaw);
	void CarryOn(USceneComponent& Parent, float BodyHalfHeight);

	UPROPERTY(EditAnywhere, Category="Belongings")
	EYUFSBelongingsPropKind Kind = EYUFSBelongingsPropKind::Backpack;
	UPROPERTY(EditAnywhere, Category="Belongings")
	TObjectPtr<UMaterialInterface> FabricMaterial;

protected:
	virtual void BeginPlay() override;
private:
	void RefreshVisuals();
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	UPROPERTY()
	TObjectPtr<UStaticMesh> Sphere;
	UPROPERTY()
	TObjectPtr<UStaticMesh> Cube;
};
