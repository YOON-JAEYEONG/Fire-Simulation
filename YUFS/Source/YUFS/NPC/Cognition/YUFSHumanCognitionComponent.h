#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "NPC/Cognition/YUFSHumanCognitionTypes.h"
#include "YUFSHumanCognitionComponent.generated.h"

class FYUFSDeterministicRngSet;
class UYUFSBehaviorPolicy;
struct FYUFSNPCObservation;

/**
 * Decision-team owned human cognition state. It consumes observations only and never
 * moves the pawn, plays animation, or scans interactable actors.
 */
UCLASS(ClassGroup=(YUFS), meta=(BlueprintSpawnableComponent))
class YUFS_API UYUFSHumanCognitionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UYUFSHumanCognitionComponent();

	void Initialize(int32 StableNpcId, FYUFSDeterministicRngSet& RandomSource);
	void UpdateCognition(float DeltaTime, const FYUFSNPCObservation& Observation);
	void NotifyPlanChanged();

	const FYUFSCognitiveState& GetCognitiveState() const { return CognitiveState; }
	const FYUFSHumanTraits& GetTraits() const { return Traits; }
	bool DidEvidenceChange() const { return bEvidenceChanged; }
	bool ShouldConsiderFreeze() const { return bFreezeCueThisUpdate; }
	bool ConsumeFreezeCue();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="NPC|Cognition")
	TObjectPtr<UYUFSBehaviorPolicy> PolicyAsset = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPC|Cognition")
	FYUFSHumanTraits Traits;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPC|Cognition")
	bool bGenerateDeterministicTraitVariation = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPC|Cognition", meta=(ClampMin="0.0", ClampMax="0.5"))
	float TraitVariationHalfRange = 0.15f;

private:
	static float MoveToward(float Current, float Target, float Rate, float DeltaTime);
	uint32 BuildEvidenceSignature(const FYUFSNPCObservation& Observation, EYUFSPerceivedPhysicalSeverity Severity) const;
	EYUFSPerceivedPhysicalSeverity ResolvePhysicalSeverity(const FYUFSNPCObservation& Observation) const;
	FName ResolveEvidenceTrigger(uint32 PreviousSignature, uint32 NewSignature) const;

	FYUFSCognitiveState CognitiveState;
	uint32 LastEvidenceSignature = MAX_uint32;
	EYUFSPerceivedPhysicalSeverity PreviousSeverity = EYUFSPerceivedPhysicalSeverity::None;
	bool bInitialized = false;
	bool bEvidenceChanged = false;
	bool bFreezeCueThisUpdate = false;
};
