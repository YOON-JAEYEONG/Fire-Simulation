#pragma once

#include "Components/ActorComponent.h"
#include "Core/YUFSTypes.h"
#include "CoreMinimal.h"
#include "YUFSIntentComponent.generated.h"

class UYUFSBeliefComponent;
class FYUFSDeterministicRngSet;
struct FYUFSNPCObservation;

/** 위험 확신을 장기 행동 목표로 변환하는 의도 계층. */
UCLASS(ClassGroup=(YUFS), meta=(BlueprintSpawnableComponent))
class YUFS_API UYUFSIntentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UYUFSIntentComponent();

	void UpdateIntent(
		float DeltaTime,
		const FYUFSNPCObservation& Observation,
		const UYUFSBeliefComponent& Belief,
		bool bHasSafeExit,
		FYUFSDeterministicRngSet& RandomSource);

	EYUFSIntent GetCurrentIntent() const { return CurrentIntent; }
	EYUFSIntent GetPreviousIntent() const { return PreviousIntent; }
	bool DidIntentChange() const { return bIntentChanged; }
	const FString& GetLastTrigger() const { return LastTrigger; }
	uint64 GetDecisionIndex() const { return DecisionIndex; }
	int32 GetPreActionTargetCount() const { return PreActionTargetCount; }
	int32 GetPreActionCompletedCount() const { return PreActionCompletedCount; }
	void NotifyPreActionCompleted(bool bHasSafeExit);

	UPROPERTY(EditAnywhere, Category="Intent", meta=(ClampMin="0.1"))
	float ReassessmentIntervalSeconds = 1.0f;

	UPROPERTY(EditAnywhere, Category="Intent", meta=(ClampMin="0.0", ClampMax="1.0"))
	float PrepareProbabilityThreshold = 0.35f;

	UPROPERTY(EditAnywhere, Category="Intent")
	bool bLockEvacuationCommit = true;

private:
	void SetIntent(EYUFSIntent NewIntent, const TCHAR* Trigger);
	int32 SamplePreActionTargetCount(FYUFSDeterministicRngSet& RandomSource) const;

	EYUFSIntent CurrentIntent = EYUFSIntent::Observe;
	EYUFSIntent PreviousIntent = EYUFSIntent::Observe;
	FString LastTrigger = TEXT("Initial");
	float ReassessmentAccumulator = 0.f;
	uint32 LastCueMask = MAX_uint32;
	uint64 DecisionIndex = 0;
	int32 PreActionTargetCount = 0;
	int32 PreActionCompletedCount = 0;
	bool bReappraisalRequested = false;
	bool bIntentChanged = false;
};
