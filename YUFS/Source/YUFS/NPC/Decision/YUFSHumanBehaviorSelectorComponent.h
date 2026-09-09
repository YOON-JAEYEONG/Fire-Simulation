#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "NPC/Integration/YUFSTeamIntegrationTypes.h"
#include "YUFSHumanBehaviorSelectorComponent.generated.h"

class FYUFSDeterministicRngSet;
class UYUFSBehaviorPolicy;
struct FYUFSCognitiveState;
struct FYUFSHumanTraits;
struct FYUFSNPCObservation;

/**
 * Selects a stable semantic behavior for the current appraisal. It does not perform
 * navigation, animation, object discovery, reservation, or interaction execution.
 */
UCLASS(ClassGroup=(YUFS), meta=(BlueprintSpawnableComponent))
class YUFS_API UYUFSHumanBehaviorSelectorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UYUFSHumanBehaviorSelectorComponent();

	const FYUFSBehaviorDecision& ResolveDecision(
		EYUFSAction ProposedLegacyAction,
		EYUFSIntent Intent,
		const FYUFSNPCObservation& Observation,
		const FYUFSCognitiveState& Cognition,
		const FYUFSHumanTraits& Traits,
		const FYUFSInteractionOpportunitySnapshot& Opportunities,
		bool bFreezeCue,
		FYUFSDeterministicRngSet& RandomSource);

	void NotifyTaskFinished(EYUFSActionTask FinishedTask);
	void RequestReselection(FName Reason);

	const FYUFSBehaviorDecision& GetCurrentDecision() const { return CurrentDecision; }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="NPC|Behavior")
	TObjectPtr<UYUFSBehaviorPolicy> PolicyAsset = nullptr;
	/** Opt-in presentation: choose suppression when eligible; all safety checks still apply. */
	UPROPERTY(EditAnywhere, Category="NPC|Behavior|Demonstration")
	bool bDemonstrateSuppressionWhenEligible = false;
	/** -1 preserves policy weights; otherwise one probability draw per eligible encounter. */
	UPROPERTY(EditAnywhere, Category="NPC|Behavior", meta=(ClampMin="-1.0", ClampMax="1.0"))
	float SuppressionProbabilityOverride = -1.f;

private:
	struct FCandidate
	{
		EYUFSHighLevelBehavior Behavior = EYUFSHighLevelBehavior::None;
		EYUFSAction LegacyAction = EYUFSAction::Idle;
		EYUFSActionTask Task = EYUFSActionTask::None;
		float Weight = 0.f;
		FName Reason = NAME_None;
	};

	FYUFSBehaviorDecision BuildIntentDecision(EYUFSAction ProposedLegacyAction, EYUFSIntent Intent) const;
	FYUFSBehaviorDecision SelectPreAction(
		const FYUFSNPCObservation& Observation,
		const FYUFSCognitiveState& Cognition,
		const FYUFSHumanTraits& Traits,
		const FYUFSInteractionOpportunitySnapshot& Opportunities,
		bool bFreezeCue,
		FYUFSDeterministicRngSet& RandomSource) const;
	static bool IsLegacyNavigationAction(EYUFSAction Action);

	FYUFSBehaviorDecision CurrentDecision;
	EYUFSIntent LastIntent = EYUFSIntent::Observe;
	EYUFSHighLevelBehavior LastCompletedBehavior = EYUFSHighLevelBehavior::None;
	int64 LastKnowledgeRevision = 0;
	int64 NextRevision = 1;
	bool bNeedsReselection = true;
	FName PendingReselectionReason = NAME_None;
	TSet<FString> ConsideredSuppressionPairs;
};
