#pragma once
#include "Components/ActorComponent.h"
#include "YUFSLocalMovementComponent.generated.h"
class AYUFSEvacuationNPC;
UENUM(BlueprintType)
enum class EYUFSLocalMovementState : uint8 { Following, Yielding, Recovering, Blocked, Passing };

// Local traffic and physical recovery. The navigator still owns the destination and global route.
UCLASS(ClassGroup=(YUFS), meta=(BlueprintSpawnableComponent))
class YUFS_API UYUFSLocalMovementComponent : public UActorComponent
{
	GENERATED_BODY()
	friend struct FYUFSTrafficRegressionAccess;
	friend struct FYUFSTrafficFlowTestAccess;
public:
	UYUFSLocalMovementComponent();
	void Reset();
	FVector ResolveDirection(FVector DesiredDirection, float Dt, int32 Frame);
	bool TryRecovery(FVector DesiredDirection,int32 Frame,const AYUFSEvacuationNPC* GiveWayTo=nullptr);
	bool TickRecovery(float Dt,int32 Frame);
	bool IsRecovering() const { return State==EYUFSLocalMovementState::Recovering; }
	bool IsPassing() const { return State==EYUFSLocalMovementState::Passing; }
	/** Stepping out of someone's way (see RequestMakeWay). */
	bool IsMakingWay() const { return bMakingWay && IsRecovering(); }
	bool IsDeliberatelyWaiting() const { return State==EYUFSLocalMovementState::Yielding || IsRecovering() || bPacingBehind || (State==EYUFSLocalMovementState::Blocked && YieldingTo.IsValid()); }
	/**
	 * Someone is waiting to get past this person. If this person is not walking anywhere (still
	 * deciding, gathering things), they step out of the requester's way. Returns true if they do.
	 */
	bool RequestMakeWay(const AYUFSEvacuationNPC* Requester, FVector RequesterDirection, int32 Frame);
	UFUNCTION(BlueprintPure) EYUFSLocalMovementState GetState() const { return State; }
	UFUNCTION(BlueprintPure) FString GetYieldingTo() const;
	UPROPERTY(EditAnywhere, Category="Traffic") float LookAheadCm=220.f;
	UPROPERTY(EditAnywhere, Category="Traffic") float MinYieldSeconds=0.8f;
	/** Walk around a person standing in the way (or overtake a much slower one) instead of waiting behind them. */
	UPROPERTY(EditAnywhere, Category="Traffic") bool bPassPeopleInTheWay=true;
	/** Preferred surface-to-surface gap kept from the person being passed; less is used where the corridor is narrow. */
	UPROPERTY(EditAnywhere, Category="Traffic") float PassClearanceCm=20.f;
	/** Someone ahead going the same way slower than this fraction of our pace is overtaken when there is room. */
	UPROPERTY(EditAnywhere, Category="Traffic") float OvertakeSpeedFraction=0.6f;
	/** Walking behind someone: the gap below which we stop, and the gap from which we match their pace. */
	UPROPERTY(EditAnywhere, Category="Traffic") float QueueStopGapCm=12.f;
	UPROPERTY(EditAnywhere, Category="Traffic") float QueuePaceGapCm=60.f;
	/** Waiting this long behind someone who is not going anywhere, ask them to step aside. */
	UPROPERTY(EditAnywhere, Category="Traffic") float MakeWayAfterSeconds=0.5f;
	UPROPERTY(EditAnywhere, Category="Recovery") int32 MaxRecoveryAttempts=4;
	UPROPERTY(EditAnywhere, Category="Recovery") float RecoveryCooldownSeconds=1.5f;
	static bool TrajectoriesConflict(FVector A,FVector DA,float RA,FVector B,FVector DB,float RB,float LookAhead);
	static bool ShouldYieldTo(FVector A,FVector DA,uint32 IDA,FVector B,FVector DB,uint32 IDB);
	bool IsCycleLeader(const AYUFSEvacuationNPC* Blocker) const;
	// A reserved route is not a moving trajectory while the person operates a door or gives assistance.
	static bool IsMovingPeer(bool bFollowingPath, bool bInteractionHoldingPosition)
	{ return bFollowingPath && !bInteractionHoldingPosition; }
	bool IsCandidateReachable(FVector CandidateFeet,int32 Frame,FVector& ProjectedFeet) const;
	bool IsPhysicalCorridorClear(FVector TargetFeet) const;
private:
	void SetState(EYUFSLocalMovementState NewState);
	/** Room to walk around Other on one side of Desired: the side (+1/-1) and centre offset to keep. */
	bool FindPassSide(const AYUFSEvacuationNPC* Other, FVector Desired, int32 Frame, float& OutSide, float& OutOffset) const;
	/** Steering direction that keeps right of someone coming the other way (left if the right is blocked). */
	bool FindKeepRight(FVector Desired, int32 Frame, const AYUFSEvacuationNPC* Other, FVector& OutDirection) const;
	/** Steering around the person being passed, aiming beside and then beyond them. */
	FVector PassSteering(const AYUFSEvacuationNPC* Other, FVector Desired) const;
	/**
	 * A short step is walkable: clear of walls and people (Ignore aside), floor under the whole way
	 * within MaxRise of our feet (defaults to the step height), on the navmesh and not into smoke.
	 */
	bool IsStepClear(FVector TargetFeet, int32 Frame, const AActor* Ignore, float MaxRise=-1.f) const;
	/** Floor under the straight line FromFeet->ToFeet, every 25 cm, within MaxRise of FeetZ. */
	bool HasFloorAlong(FVector FromFeet, FVector ToFeet, float FeetZ, float MaxRise, const AActor* Ignore) const;
	TWeakObjectPtr<AYUFSEvacuationNPC> YieldingTo;
	TWeakObjectPtr<AYUFSEvacuationNPC> PassingAround;
	TWeakObjectPtr<const AYUFSEvacuationNPC> PassCheckedFor;
	TWeakObjectPtr<const AYUFSEvacuationNPC> PacedBehind;
	EYUFSLocalMovementState State=EYUFSLocalMovementState::Following;
	FVector RecoveryTarget=FVector::ZeroVector;
	FVector LastProgressPosition=FVector::ZeroVector;
	FVector LastBlockerPosition=FVector::ZeroVector;
	float YieldTime=0.f;
	float RecoveryTime=0.f;
	float Cooldown=0.f;
	float PassSide=1.f;
	float PassOffset=0.f;
	float PassTime=0.f;
	float PassCheckAgainAt=0.f;
	float MakeWayReadyAt=0.f;
	bool bPacingBehind=false;
	bool bMakingWay=false;
	int32 Attempts=0;
};
