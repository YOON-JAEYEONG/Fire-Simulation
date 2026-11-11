#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "NPC/Cognition/YUFSHumanCognitionTypes.h"
#include "YUFSBehaviorPolicy.generated.h"

/** Data-driven project priors. These are calibration inputs, not population rates. */
UCLASS(BlueprintType)
class YUFS_API UYUFSBehaviorPolicy : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Policy")
	FName PolicyVersion = TEXT("npc-behavior-1");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Traits")
	FYUFSHumanTraits DefaultTraits;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cognition", meta=(ClampMin="0.01"))
	float CognitionResponseRate = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cognition", meta=(ClampMin="0.0", ClampMax="1.0"))
	float ConfirmedSmokeThreshold = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cognition", meta=(ClampMin="0.0", ClampMax="1.0"))
	float LifeRiskSmokeThreshold = 0.70f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cognition", meta=(ClampMin="0.0", ClampMax="1.0"))
	float LifeRiskTemperatureThreshold = 0.80f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Task Weights", meta=(ClampMin="0.0"))
	float SeekInformationWeight = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Task Weights", meta=(ClampMin="0.0"))
	float WaitOrContinueWeight = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Task Weights", meta=(ClampMin="0.0"))
	float RetrieveBelongingsWeight = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Task Weights", meta=(ClampMin="0.0"))
	float WarnOrAssistWeight = 0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Task Weights", meta=(ClampMin="0.0"))
	float AttemptSuppressionWeight = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Task Weights", meta=(ClampMin="0.0"))
	float ObserveOrRecordWeight = 0.03f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Task Weights", meta=(ClampMin="0.0", ClampMax="1.0"))
	float FreezeBaseProbability = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Task Selection", meta=(ClampMin="0.0"))
	float RepetitionPenalty = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Task Selection")
	bool bUseLegacyBelongingsAssumption = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Task Selection", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MinimumSuppressionTraining = 0.55f;

	/** Simulation prior per newly encountered fire/tool pair; not an observed population rate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Task Selection", meta=(ClampMin="0.0", ClampMax="1.0"))
	float EvacuatingSuppressionProbability = 0.25f;
};
