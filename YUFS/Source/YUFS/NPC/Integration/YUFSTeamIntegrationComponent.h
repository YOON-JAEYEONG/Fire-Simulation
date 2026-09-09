#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "NPC/Cognition/YUFSHumanCognitionTypes.h"
#include "NPC/Integration/YUFSTeamIntegrationTypes.h"
#include "YUFSTeamIntegrationComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FYUFSNavigationDirectiveChanged,
	const FYUFSNavigationDirective&,
	Directive);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FYUFSMotionDirectiveChanged,
	const FYUFSMotionDirective&,
	Directive);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FYUFSInteractionDirectiveChanged,
	const FYUFSInteractionDirective&,
	Directive);

/**
 * Merge boundary between decision, path finding, motion, and interaction owners.
 * It publishes immutable semantic directives and accepts explicit feedback.
 */
UCLASS(ClassGroup=(YUFS), meta=(BlueprintSpawnableComponent))
class YUFS_API UYUFSTeamIntegrationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UYUFSTeamIntegrationComponent();

	void PublishDecision(
		int32 StableNpcId,
		const FYUFSBehaviorDecision& Decision,
		EYUFSIntent Intent,
		EYUFSBehaviorState BehaviorState,
		const FVector& DestinationHint,
		bool bHasSafeExit,
		const FYUFSCognitiveState& Cognition);

	UFUNCTION(BlueprintCallable, Category="NPC|Team Integration")
	void SubmitInteractionOpportunities(const FYUFSInteractionOpportunitySnapshot& Snapshot);

	UFUNCTION(BlueprintCallable, Category="NPC|Team Integration")
	void SubmitNavigationFeedback(const FYUFSTeamRequestFeedback& Feedback);

	UFUNCTION(BlueprintCallable, Category="NPC|Team Integration")
	void SubmitMotionFeedback(const FYUFSTeamRequestFeedback& Feedback);

	UFUNCTION(BlueprintCallable, Category="NPC|Team Integration")
	void SubmitInteractionFeedback(const FYUFSTeamRequestFeedback& Feedback);

	const FYUFSInteractionOpportunitySnapshot& GetInteractionOpportunities() const { return InteractionOpportunities; }
	const FYUFSNavigationDirective& GetNavigationDirective() const { return NavigationDirective; }
	const FYUFSMotionDirective& GetMotionDirective() const { return MotionDirective; }
	const FYUFSInteractionDirective& GetInteractionDirective() const { return InteractionDirective; }
	const FYUFSTeamRequestFeedback& GetNavigationFeedback() const { return NavigationFeedback; }
	const FYUFSTeamRequestFeedback& GetMotionFeedback() const { return MotionFeedback; }
	const FYUFSTeamRequestFeedback& GetInteractionFeedback() const { return InteractionFeedback; }
	int64 GetFeedbackGeneration() const { return FeedbackGeneration; }

	UPROPERTY(BlueprintAssignable, Category="NPC|Team Integration")
	FYUFSNavigationDirectiveChanged OnNavigationDirectiveChanged;

	UPROPERTY(BlueprintAssignable, Category="NPC|Team Integration")
	FYUFSMotionDirectiveChanged OnMotionDirectiveChanged;

	UPROPERTY(BlueprintAssignable, Category="NPC|Team Integration")
	FYUFSInteractionDirectiveChanged OnInteractionDirectiveChanged;

private:
	static EYUFSNavigationGoal MapNavigationGoal(EYUFSHighLevelBehavior Behavior);
	static EYUFSMotionSemantic MapMotionSemantic(
		EYUFSHighLevelBehavior Behavior,
		EYUFSBehaviorState BehaviorState);
	FYUFSInteractionDirective BuildInteractionDirective(
		int32 StableNpcId,
		const FYUFSBehaviorDecision& Decision) const;

	FYUFSInteractionOpportunitySnapshot InteractionOpportunities;
	FYUFSNavigationDirective NavigationDirective;
	FYUFSMotionDirective MotionDirective;
	FYUFSInteractionDirective InteractionDirective;
	FYUFSTeamRequestFeedback NavigationFeedback;
	FYUFSTeamRequestFeedback MotionFeedback;
	FYUFSTeamRequestFeedback InteractionFeedback;
	int64 NavigationRevision = 0;
	int64 MotionRevision = 0;
	int64 InteractionRevision = 0;
	int64 FeedbackGeneration = 0;
};
