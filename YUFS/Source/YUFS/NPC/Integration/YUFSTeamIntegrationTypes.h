#pragma once

#include "CoreMinimal.h"
#include "Core/YUFSTypes.h"
#include "YUFSTeamIntegrationTypes.generated.h"

/**
 * Decision-owned semantic behavior. It is intentionally separate from EYUFSAction,
 * whose numeric values are part of the existing ONNX/animation contract.
 */
UENUM(BlueprintType)
enum class EYUFSHighLevelBehavior : uint8
{
	None,
	ContinueRoutine,
	WaitObserve,
	WaitForAuthority,
	SeekInformation,
	RetrieveBelongings,
	WarnOthers,
	AssistOther,
	ObserveOrRecord,
	AttemptSuppression,
	Freeze,
	EvacuateNearest,
	EvacuateFamiliar,
	FollowCrowd,
	Shelter,
	Reenter,
	Incapacitated
};

UENUM(BlueprintType)
enum class EYUFSNavigationGoal : uint8
{
	None,
	SafeExit,
	FamiliarExit,
	CrowdDestination,
	AssistTarget,
	InvestigateTarget,
	InteractionTarget,
	ShelterLocation,
	ReentryTarget
};

UENUM(BlueprintType)
enum class EYUFSMotionSemantic : uint8
{
	Idle,
	Walk,
	Run,
	LookAround,
	Wait,
	GatherBelongings,
	Warn,
	Assist,
	Record,
	Extinguish,
	OperateDoor,
	Crawl,
	Cough,
	Freeze
};

UENUM(BlueprintType)
enum class EYUFSInteractionGoal : uint8
{
	None,
	InspectHazard,
	OpenDoor,
	HoldDoor,
	PassDoor,
	RetrieveBelongings,
	AcquireExtinguisher,
	SuppressFire,
	AssistPerson
};

UENUM(BlueprintType)
enum class EYUFSTeamRequestStatus : uint8
{
	None,
	Accepted,
	InProgress,
	Completed,
	Failed,
	Blocked,
	Cancelled
};

USTRUCT(BlueprintType)
struct YUFS_API FYUFSBehaviorDecision
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;

	UPROPERTY(BlueprintReadOnly)
	EYUFSHighLevelBehavior Behavior = EYUFSHighLevelBehavior::None;

	UPROPERTY(BlueprintReadOnly)
	EYUFSAction LegacyAction = EYUFSAction::Idle;

	UPROPERTY(BlueprintReadOnly)
	EYUFSActionTask DesiredTask = EYUFSActionTask::None;

	UPROPERTY(BlueprintReadOnly)
	FName Reason = NAME_None;
};

/** Decision -> route-finding team. The receiver owns path queries and movement targets. */
USTRUCT(BlueprintType)
struct YUFS_API FYUFSNavigationDirective
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 StableNpcId = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly)
	EYUFSNavigationGoal Goal = EYUFSNavigationGoal::None;

	UPROPERTY(BlueprintReadOnly)
	FVector DestinationHint = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	FName TargetStableId = NAME_None;

	UPROPERTY(BlueprintReadOnly)
	bool bAllowHazardReroute = true;

	UPROPERTY(BlueprintReadOnly)
	float MaxPerceivedRisk = 1.f;

	UPROPERTY(BlueprintReadOnly)
	FName Reason = NAME_None;
};

/** Decision -> motion team. The receiver owns animation, montage, IK and presentation. */
USTRUCT(BlueprintType)
struct YUFS_API FYUFSMotionDirective
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 StableNpcId = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly)
	EYUFSMotionSemantic Semantic = EYUFSMotionSemantic::Idle;

	UPROPERTY(BlueprintReadOnly)
	EYUFSAction LegacyAction = EYUFSAction::Idle;

	UPROPERTY(BlueprintReadOnly)
	EYUFSBehaviorState BehaviorState = EYUFSBehaviorState::Normal;

	UPROPERTY(BlueprintReadOnly)
	float SpeedScale = 1.f;

	UPROPERTY(BlueprintReadOnly)
	bool bLoop = true;

	UPROPERTY(BlueprintReadOnly)
	FName Reason = NAME_None;
};

/** Decision -> interaction team. The receiver owns discovery, reservation and execution. */
USTRUCT(BlueprintType)
struct YUFS_API FYUFSInteractionDirective
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 StableNpcId = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly)
	EYUFSInteractionGoal Goal = EYUFSInteractionGoal::None;

	UPROPERTY(BlueprintReadOnly)
	FName TargetStableId = NAME_None;

	UPROPERTY(BlueprintReadOnly)
	FVector TargetLocationHint = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	bool bRequiresReservation = false;

	UPROPERTY(BlueprintReadOnly)
	float MaxSearchRadiusCm = 1500.f;

	UPROPERTY(BlueprintReadOnly)
	FName Reason = NAME_None;
};

/**
 * Interaction team -> decision. This must contain perceived or remembered knowledge,
 * never omniscient world truth that the NPC has not discovered.
 */
USTRUCT(BlueprintType)
struct YUFS_API FYUFSInteractionOpportunitySnapshot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int64 KnowledgeRevision = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bBelongingsKnown = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName BelongingsStableId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector BelongingsLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bDoorActionRequired = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bAssistPersonKnown = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName AssistPersonStableId = NAME_None;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector AssistPersonLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName DoorStableId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector DoorUseLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bExtinguisherKnownAvailable = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName ExtinguisherStableId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector ExtinguisherLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bSuppressibleFireKnown = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName FireStableId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector FireLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bHoldingExtinguisher = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bSafeRetreatKnown = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.0", ClampMax="1.0"))
	float KnowledgeConfidence = 0.f;
};

USTRUCT(BlueprintType)
struct YUFS_API FYUFSTeamRequestFeedback
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int64 RequestRevision = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EYUFSTeamRequestStatus Status = EYUFSTeamRequestStatus::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName Reason = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector ResolvedLocation = FVector::ZeroVector;
};
