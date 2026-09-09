#pragma once

#include "CoreMinimal.h"
#include "YUFSHumanCognitionTypes.generated.h"

UENUM(BlueprintType)
enum class EYUFSPerceivedPhysicalSeverity : uint8
{
	None,
	AmbiguousAlarm,
	ConfirmedSmoke,
	ImmediateLifeThreat
};

USTRUCT(BlueprintType)
struct YUFS_API FYUFSHumanTraits
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0", ClampMax="1.0"))
	float BuildingFamiliarity = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0", ClampMax="1.0"))
	float FireTraining = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0", ClampMax="1.0"))
	float PhysicalMobility = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0", ClampMax="1.0"))
	float StressSensitivity = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0", ClampMax="1.0"))
	float RiskTolerance = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0", ClampMax="1.0"))
	float AuthorityTrust = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0", ClampMax="1.0"))
	float SocialConformity = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0", ClampMax="1.0"))
	float HelpingTendency = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0", ClampMax="1.0"))
	float GroupAttachment = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0", ClampMax="1.0"))
	float HabitStrength = 0.5f;
};

USTRUCT(BlueprintType)
struct YUFS_API FYUFSCognitiveState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	float PerceivedRisk = 0.f;

	UPROPERTY(BlueprintReadOnly)
	float Urgency = 0.f;

	UPROPERTY(BlueprintReadOnly)
	float Stress = 0.f;

	UPROPERTY(BlueprintReadOnly)
	float CognitiveLoad = 0.f;

	UPROPERTY(BlueprintReadOnly)
	float SituationConfidence = 0.f;

	UPROPERTY(BlueprintReadOnly)
	float NormalcyBias = 1.f;

	UPROPERTY(BlueprintReadOnly)
	float CurrentPlanCommitment = 0.f;

	UPROPERTY(BlueprintReadOnly)
	EYUFSPerceivedPhysicalSeverity PhysicalSeverity = EYUFSPerceivedPhysicalSeverity::None;

	UPROPERTY(BlueprintReadOnly)
	int64 EvidenceRevision = 0;

	UPROPERTY(BlueprintReadOnly)
	FName LastEvidenceTrigger = NAME_None;
};
