#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/YUFSTypes.h"
#include "YUFSActionAnimationComponent.generated.h"

class UAnimationAsset;
class USkeletalMeshComponent;

USTRUCT(BlueprintType)
struct FYUFSActionAnimationBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation")
	EYUFSAction Action = EYUFSAction::Idle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation")
	TSoftObjectPtr<UAnimationAsset> Animation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation")
	bool bLoop = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation", meta=(ClampMin="0.1", ClampMax="3.0"))
	float PlayRate = 1.f;
};

/**
 * Data-driven bridge from the NPC decision action to a visible skeletal animation.
 *
 * Optional single-node playback for compatible per-action sequences. JJW NPCs can
 * instead retain their existing Animation Blueprint: this component takes control
 * only when all effective action/crawl/incapacitation bindings match the mesh's
 * skeleton. It never changes the character mesh, navigation, or behavior state.
 */
UCLASS(ClassGroup=(YUFS), meta=(BlueprintSpawnableComponent))
class YUFS_API UYUFSActionAnimationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UYUFSActionAnimationComponent();

	void Initialize(USkeletalMeshComponent* InMesh, int32 StableNpcId);
	void ApplyAction(EYUFSAction Action, EYUFSBehaviorState BehaviorState, bool bForce = false);

	/** Read-only preflight of the actual configured bindings, including custom overrides. */
	UFUNCTION(BlueprintPure, Category="NPC|Animation")
	bool CanUseNativeAnimations(USkeletalMeshComponent* InMesh) const;

	UFUNCTION(BlueprintPure, Category="NPC|Animation")
	bool HasAnimationForAction(EYUFSAction Action) const;

	UFUNCTION(BlueprintPure, Category="NPC|Animation")
	FString GetActiveAnimationName() const;

	UFUNCTION(BlueprintPure, Category="NPC|Animation")
	FString GetConfiguredAnimationPath(EYUFSAction Action) const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPC|Animation")
	TArray<FYUFSActionAnimationBinding> ActionAnimations;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPC|Animation")
	TSoftObjectPtr<UAnimationAsset> CrawlingAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPC|Animation")
	TSoftObjectPtr<UAnimationAsset> IncapacitatedAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPC|Animation", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MaximumLoopStartOffsetFraction = 0.65f;

private:
	const FYUFSActionAnimationBinding* FindBinding(EYUFSAction Action) const;
	void AddDefaultBinding(EYUFSAction Action, const TCHAR* AssetPath, bool bLoop, float PlayRate);

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> Mesh = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UAnimationAsset> ActiveAnimation = nullptr;

	EYUFSAction ActiveAction = EYUFSAction::Idle;
	EYUFSBehaviorState ActiveBehaviorState = EYUFSBehaviorState::Normal;
	int32 StableId = 0;
	bool bInitialized = false;
};
