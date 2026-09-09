#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "YUFSSuppressibleFireSource.generated.h"

class UMaterialInstanceDynamic;
class UPointLightComponent;
class USceneComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/** A small, explicitly suppressible demo fire. It does not rewrite prerecorded smoke data. */
UCLASS(BlueprintType)
class YUFS_API AYUFSSuppressibleFireSource : public AActor
{
	GENERATED_BODY()

public:
	AYUFSSuppressibleFireSource();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable, Category="Fire|Suppression")
	float ApplySuppression(float Amount);

	UFUNCTION(BlueprintPure, Category="Fire|Suppression")
	float GetIntensityNormalized() const { return IntensityNormalized; }

	UFUNCTION(BlueprintPure, Category="Fire|Suppression")
	bool IsExtinguished() const { return IntensityNormalized <= KINDA_SMALL_NUMBER; }

private:
	void RefreshVisualState();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> FlameOuter;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> FlameInner;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> FuelBase;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> FireLight;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> StatusText;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> OuterMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> InnerMaterial;

	UPROPERTY(VisibleAnywhere, Category="Fire|Suppression", meta=(ClampMin="0.0", ClampMax="1.0"))
	float IntensityNormalized = 1.f;

	float VisualTime = 0.f;
};
