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
	FVector2D RememberDelaySeconds = FVector2D(1.5f, 4.f);

	/** Reaching this close to an exit makes the NPC remember the bag at once (noticed at the door). */
	UPROPERTY(EditAnywhere, Category="NPC|Belongings", meta=(ClampMin="0.0"))
	float RememberNearExitCm = 900.f;

	/**
	 * After remembering the bag the NPC keeps weighing it up for this long: a moment that
	 * looks unsafe (a puff of smoke, a risk spike) makes it hesitate, not give up for good.
	 * Only when the window closes without a safe moment is the bag abandoned.
	 */
	UPROPERTY(EditAnywhere, Category="NPC|Belongings", meta=(ClampMin="0.0"))
	float DecisionWindowSeconds = 15.f;

	/** Perceived risk above which the NPC will not turn back. Observed smoke/heat always aborts. */
	UPROPERTY(EditAnywhere, Category="NPC|Belongings", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MaxRiskToReturn = 0.85f;

	/**
	 * Scenario design value: share of bag owners for whom general unease (perceived risk) is
	 * not a reason to leave the bag - only smoke or heat they actually see stops them.
	 * Risk perception saturates quickly once a fire is noticed, so without this almost nobody
	 * would ever go back.
	 */
	UPROPERTY(EditAnywhere, Category="NPC|Belongings", meta=(ClampMin="0.0", ClampMax="1.0"))
	float RiskIgnoringShare = 0.5f;

	/** A return path longer than this is not worth it. */
	UPROPERTY(EditAnywhere, Category="NPC|Belongings", meta=(ClampMin="100.0"))
	float MaxReturnPathCm = 3500.f;

	UPROPERTY(EditAnywhere, Category="NPC|Belongings", meta=(ClampMin="1.0"))
	float MaxRetrievalSeconds = 60.f;

	UPROPERTY(EditAnywhere, Category="NPC|Belongings", meta=(ClampMin="0.1"))
	float PickupSeconds = 2.0f;

	UPROPERTY(EditAnywhere, Category="NPC|Belongings", meta=(ClampMin="10.0"))
	float PickupRadiusCm = 90.f;

	/**
	 * A bag placed in the level within this distance of where the NPC starts becomes that NPC's
	 * left-behind bag (nearest unclaimed one, same floor). When a level has placed bags, no extra
	 * bags are spawned at random, so the designer's placement is exactly what appears.
	 */
	UPROPERTY(EditAnywhere, Category="NPC|Belongings", meta=(ClampMin="50.0"))
	float LevelBagClaimRadiusCm = 800.f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	void RollForEpisode();
	bool PlaceBag();
	bool ClaimLevelBag();
	FVector GetEpisodeOrigin() const;
	/** bCommitted: already on the way back - general unease no longer stops it, only observed cues do. */
	bool IsReturnSafe(const FYUFSNPCObservation& Observation, FName& OutReason, bool bCommitted = false) const;
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
	float NextDecisionAt = -1.f;
	bool bIgnoresRisk = false;
	FName HesitationReason = NAME_None;
	int64 ExecutionRevision = 0;
	bool bRolled = false;
	bool bWantsBag = false;
	int32 EpisodeCounter = 0;
};
