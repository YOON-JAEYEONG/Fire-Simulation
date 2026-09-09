#pragma once
#include "Components/ActorComponent.h"
#include "YUFSLocalMovementComponent.generated.h"
class AYUFSEvacuationNPC;
UENUM(BlueprintType)
enum class EYUFSLocalMovementState : uint8 { Following, Yielding, Recovering, Blocked };

// Local traffic and physical recovery. The navigator still owns the destination and global route.
UCLASS(ClassGroup=(YUFS), meta=(BlueprintSpawnableComponent))
class YUFS_API UYUFSLocalMovementComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UYUFSLocalMovementComponent();
	void Reset();
	FVector ResolveDirection(FVector DesiredDirection, float Dt, int32 Frame);
	bool TryRecovery(FVector DesiredDirection,int32 Frame);
	bool TickRecovery(float Dt,int32 Frame);
	bool IsRecovering() const { return State==EYUFSLocalMovementState::Recovering; }
	bool IsDeliberatelyWaiting() const { return State==EYUFSLocalMovementState::Yielding || IsRecovering() || (State==EYUFSLocalMovementState::Blocked && YieldingTo.IsValid()); }
	UFUNCTION(BlueprintPure) EYUFSLocalMovementState GetState() const { return State; }
	UFUNCTION(BlueprintPure) FString GetYieldingTo() const;
	UPROPERTY(EditAnywhere, Category="Traffic") float LookAheadCm=220.f;
	UPROPERTY(EditAnywhere, Category="Traffic") float MinYieldSeconds=0.8f;
	UPROPERTY(EditAnywhere, Category="Recovery") int32 MaxRecoveryAttempts=4;
	UPROPERTY(EditAnywhere, Category="Recovery") float RecoveryCooldownSeconds=1.5f;
	static bool TrajectoriesConflict(FVector A,FVector DA,float RA,FVector B,FVector DB,float RB,float LookAhead);
	static bool ShouldYieldTo(FVector A,FVector DA,uint32 IDA,FVector B,FVector DB,uint32 IDB);
	bool IsCandidateReachable(FVector CandidateFeet,int32 Frame,FVector& ProjectedFeet) const;
	bool IsPhysicalCorridorClear(FVector TargetFeet) const;
private:
	void SetState(EYUFSLocalMovementState NewState);
	TWeakObjectPtr<AYUFSEvacuationNPC> YieldingTo;
	EYUFSLocalMovementState State=EYUFSLocalMovementState::Following;
	FVector RecoveryTarget=FVector::ZeroVector;
	FVector LastProgressPosition=FVector::ZeroVector;
	FVector LastBlockerPosition=FVector::ZeroVector;
	float YieldTime=0.f;
	float RecoveryTime=0.f;
	float Cooldown=0.f;
	int32 Attempts=0;
};
