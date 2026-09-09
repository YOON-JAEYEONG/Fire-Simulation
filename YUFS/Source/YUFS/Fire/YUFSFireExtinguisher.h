#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "YUFSFireExtinguisher.generated.h"

class UMaterialInstanceDynamic;
class USceneComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

UENUM(BlueprintType)
enum class EYUFSFireExtinguisherState : uint8
{
	Available,
	Reserved,
	Held,
	Spraying,
	Empty
};

/**
 * Lightweight runtime extinguisher used by the interaction adapter.
 * Reservation and ownership are authoritative so two NPCs cannot hold it at once.
 */
UCLASS(BlueprintType)
class YUFS_API AYUFSFireExtinguisher : public AActor
{
	GENERATED_BODY()

public:
	AYUFSFireExtinguisher();
	virtual void BeginPlay() override;

	UFUNCTION(BlueprintCallable, Category="Fire|Extinguisher")
	bool TryReserve(AActor* Requester);

	UFUNCTION(BlueprintCallable, Category="Fire|Extinguisher")
	bool PickUp(AActor* Requester, USceneComponent* AttachParent, FName SocketName);

	UFUNCTION(BlueprintCallable, Category="Fire|Extinguisher")
	bool StartSpraying(AActor* Requester);

	UFUNCTION(BlueprintCallable, Category="Fire|Extinguisher")
	void StopSpraying(AActor* Requester);

	UFUNCTION(BlueprintCallable, Category="Fire|Extinguisher")
	float ConsumeAgent(AActor* Requester, float RequestedAmount);

	UFUNCTION(BlueprintCallable, Category="Fire|Extinguisher")
	void Release(AActor* Requester);

	UFUNCTION(BlueprintPure, Category="Fire|Extinguisher")
	EYUFSFireExtinguisherState GetExtinguisherState() const { return State; }

	UFUNCTION(BlueprintPure, Category="Fire|Extinguisher")
	float GetRemainingAgentNormalized() const;

	UFUNCTION(BlueprintPure, Category="Fire|Extinguisher")
	AActor* GetOwnerActor() const { return ReservationOwner.Get(); }

	/** World-space point at the mouth of the discharge horn. */
	UFUNCTION(BlueprintPure, Category="Fire|Extinguisher")
	FVector GetNozzleWorldLocation() const;
	void ShowSprayToward(const FVector& Target);

private:
	void RefreshVisualState();
	void SetPrimaryCollisionEnabled(ECollisionEnabled::Type CollisionEnabled);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> BodyMesh;

	/** CC0 model downloaded from OpenGameArt; see SourceAssets for provenance. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> ImportedMainMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> TopMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> HandleMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> HoseMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> SprayStreamMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> NozzlePoint;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> StatusText;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ImportedVisualMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SprayMaterial;

	UPROPERTY(VisibleAnywhere, Category="Fire|Extinguisher")
	bool bUsingImportedVisual = false;

	UPROPERTY(VisibleAnywhere, Category="Fire|Extinguisher")
	EYUFSFireExtinguisherState State = EYUFSFireExtinguisherState::Available;

	UPROPERTY(EditAnywhere, Category="Fire|Extinguisher", meta=(ClampMin="0.1"))
	float AgentCapacity = 6.f;

	UPROPERTY(VisibleAnywhere, Category="Fire|Extinguisher")
	float RemainingAgent = 6.f;

	TWeakObjectPtr<AActor> ReservationOwner;
};
