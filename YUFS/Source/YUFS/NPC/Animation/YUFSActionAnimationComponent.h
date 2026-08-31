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
 * The project does not currently contain an action-aware animation blueprint. This
 * component therefore uses single-node playback with sequences authored for the
 * current NPC skeleton. It changes the animation only when the semantic action/state changes,
 * validates skeleton compatibility, and keeps preview playback separate from AI.
 */
UCLASS(ClassGroup=(YUFS), meta=(BlueprintSpawnableComponent))
class YUFS_API UYUFSActionAnimationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UYUFSActionAnimationComponent();

	void Initialize(USkeletalMeshComponent* InMesh, int32 StableNpcId);
	void ApplyAction(EYUFSAction Action, EYUFSBehaviorState BehaviorState, bool bForce = false);

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
