#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPC/Integration/YUFSTeamIntegrationTypes.h"
#include "YUFSBelongingsRetrievalComponent.generated.h"

class AYUFSEvacuationNPC;
class AYUFSBelongingsBag;
struct FYUFSNPCObservation;

UENUM(BlueprintType)
enum class EYUFSBelongingsRetrievalPhase : uint8
{
	/** This NPC has no left-behind belongings in the current episode. */
	None,
	/** Bag is lying at the NPC's original position; the NPC has not remembered it yet. */
	LeftBehind,
	/** Turned back during evacuation and walking to the bag. */
	Returning,
	/** Standing at the bag, picking it up. */
	PickingUp,
	/** Bag is carried; the NPC is evacuating again. */
	Carrying,
	/** Decided (or was forced) not to go back; the bag stays where it is. */
	Abandoned
};

/**
 * "Went back for my bag" behavior on the SAME evacuation character.
 * A share of NPCs leave a large bag at their original position. After evacuating for a
 * short while they remember it, turn back if the way looks safe enough, pick it up and
 * evacuate again. Any observed emergency cue aborts the return and resumes evacuation.
 */
UCLASS(ClassGroup=(YUFS), meta=(BlueprintSpawnableComponent))
class YUFS_API UYUFSBelongingsRetrievalComponent : public UActorComponent
{
	GENERATED_BODY()
	friend struct FYUFSBelongingsRetrievalTestAccess;

public:
	UYUFSBelongingsRetrievalComponent();

	/** Places the bag lazily and decides when to turn back. Call once per live Tick. */
	void Observe(float DeltaTime, const FYUFSNPCObservation& Observation);
	/** Roll and place the left-behind bag now (at spawn), so it is visible before the fire. */
	void PrepareForEpisode();
	/** Drives the retrieval. Returns true while the NPC holds position (picking up). */
	bool Execute(float DeltaTime);
	/** Abort an active return. The bag stays where it is. */
	void Cancel(bool bResumeEvacuation = true);
	/** Destroy the bag and re-roll for a new episode. */
	void ResetForEpisode();

	bool IsActive() const
	{
		return Phase == EYUFSBelongingsRetrievalPhase::Returning || Phase == EYUFSBelongingsRetrievalPhase::PickingUp;
	}
	bool IsCarrying() const { return Phase == EYUFSBelongingsRetrievalPhase::Carrying; }
	EYUFSBelongingsRetrievalPhase GetPhase() const { return Phase; }
	FVector GetMovementTarget() const { return MovementTarget; }
	AYUFSBelongingsBag* GetBag() const { return Bag.Get(); }
	FName GetLastStopReason() const { return LastStopReason; }
	FYUFSBehaviorDecision MakeDecision() const;

	UPROPERTY(EditAnywhere, Category="NPC|Belongings")
	bool bEnabled = true;

	/** Scenario design value: share of NPCs that left a bag behind. Not a measured rate. */
	UPROPERTY(EditAnywhere, Category="NPC|Belongings", meta=(ClampMin="0.0", ClampMax="1.0"))
	float LeaveBehindProbability = 0.35f;

	/** Seconds of evacuation before the NPC remembers the bag (min, max). */
	UPROPERTY(EditAnywhere, Category="NPC|Belongings")
	FVector2D RememberDelaySeconds = FVector2D(3.f, 8.f);

	/** Perceived risk above which the NPC will not turn back. Observed smoke/heat always aborts. */
	UPROPERTY(EditAnywhere, Category="NPC|Belongings", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MaxRiskToReturn = 0.85f;

	/** A return path longer than this is not worth it. */
	UPROPERTY(EditAnywhere, Category="NPC|Belongings", meta=(ClampMin="100.0"))
	float MaxReturnPathCm = 3500.f;

	UPROPERTY(EditAnywhere, Category="NPC|Belongings", meta=(ClampMin="1.0"))
	float MaxRetrievalSeconds = 60.f;

	UPROPERTY(EditAnywhere, Category="NPC|Belongings", meta=(ClampMin="0.1"))
	float PickupSeconds = 2.0f;

	UPROPERTY(EditAnywhere, Category="NPC|Belongings", meta=(ClampMin="10.0"))
	float PickupRadiusCm = 90.f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	void RollForEpisode();
	bool PlaceBag();
	bool IsReturnSafe(const FYUFSNPCObservation& Observation, FName& OutReason) const;
	bool HasSafeReturnPath(FName& OutReason) const;
	/** Danger the NPC has actually observed; unobserved or missing data is not treated as known danger. */
	bool IsObservedDangerAlong(const TArray<FVector>& FloorPoints) const;
	void BeginReturn();
	void Finish(bool bSuccess, FName Reason, bool bResumeEvacuation);
	void PublishOpportunity() const;
	void DestroyBag();

	TWeakObjectPtr<AYUFSEvacuationNPC> Npc;
	TWeakObjectPtr<AYUFSBelongingsBag> Bag;
	EYUFSBelongingsRetrievalPhase Phase = EYUFSBelongingsRetrievalPhase::None;
	FVector MovementTarget = FVector::ZeroVector;
	FName LastStopReason = NAME_None;
	float RememberDelay = 5.f;
	float EvacuationStartedAt = -1.f;
	float PhaseStartedAt = 0.f;
	float PickupElapsed = 0.f;
	float HazardRecheckTimer = 0.f;
	float FailedPathSeconds = 0.f;
	int64 ExecutionRevision = 0;
	bool bRolled = false;
	bool bWantsBag = false;
	int32 EpisodeCounter = 0;
};
