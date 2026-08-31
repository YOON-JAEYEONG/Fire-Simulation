#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "YUFSBeliefComponent.generated.h"

struct FYUFSNPCObservation;

/** 경보·연기·고열·사회 cue를 log-odds로 결합하는 위험 확신 계층. */
UCLASS(ClassGroup=(YUFS), meta=(BlueprintSpawnableComponent))
class YUFS_API UYUFSBeliefComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UYUFSBeliefComponent();

	void UpdateBelief(const FYUFSNPCObservation& Observation);

	float GetCommitProbability() const { return CommitProbability; }
	uint32 GetActiveCueMask() const { return ActiveCueMask; }
	bool HasEmergencyCue() const { return bHasEmergencyCue; }
	bool HasImmediateLifeRisk() const { return bImmediateLifeRisk; }
	bool HasVerifiedOfficialInstruction() const { return bVerifiedOfficialInstruction; }
	FString GetPolicyHash() const;

	UPROPERTY(EditAnywhere, Category="Belief|Traits")
	bool bTrainingCompleted = false;

	UPROPERTY(EditAnywhere, Category="Belief|Base Probability", meta=(ClampMin="0.001", ClampMax="0.999"))
	float NoCueBaseProbability = 0.05f;

	UPROPERTY(EditAnywhere, Category="Belief|Base Probability", meta=(ClampMin="0.001", ClampMax="0.999"))
	float AlarmBaseProbability = 0.25f;

	UPROPERTY(EditAnywhere, Category="Belief|Base Probability", meta=(ClampMin="0.001", ClampMax="0.999"))
	float ConfirmedSmokeBaseProbability = 0.65f;

	UPROPERTY(EditAnywhere, Category="Belief|Base Probability", meta=(ClampMin="0.001", ClampMax="0.999"))
	float HighHeatBaseProbability = 0.90f;

	UPROPERTY(EditAnywhere, Category="Belief|Likelihood Ratio", meta=(ClampMin="0.01"))
	float TrainingLikelihoodRatio = 1.4f;

	UPROPERTY(EditAnywhere, Category="Belief|Likelihood Ratio", meta=(ClampMin="0.01"))
	float LeaderLikelihoodRatio = 2.2f;

	UPROPERTY(EditAnywhere, Category="Belief|Likelihood Ratio", meta=(ClampMin="0.01"))
	float MovingCrowdLikelihoodRatio = 1.5f;

	UPROPERTY(EditAnywhere, Category="Belief|Threshold", meta=(ClampMin="0.0", ClampMax="1.0"))
	float ConfirmedSmokeThreshold = 0.15f;

	UPROPERTY(EditAnywhere, Category="Belief|Threshold", meta=(ClampMin="0.0", ClampMax="1.0"))
	float ImmediateLifeRiskSmokeThreshold = 0.70f;

	UPROPERTY(EditAnywhere, Category="Belief|Threshold", meta=(ClampMin="0.0", ClampMax="1.0"))
	float ImmediateLifeRiskTemperatureThreshold = 0.80f;

private:
	float CommitProbability = 0.05f;
	uint32 ActiveCueMask = 0;
	bool bHasEmergencyCue = false;
	bool bImmediateLifeRisk = false;
	bool bVerifiedOfficialInstruction = false;
};
