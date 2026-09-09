#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "YUFSNpcSuppressionComponent.generated.h"

class AYUFSEvacuationNPC;
class AYUFSFireExtinguisher;
class AYUFSHeterogeneousVolume;
struct FYUFSNPCObservation;

/** Object interaction fallback on the SAME evacuation character. No extra pawn or teleport. */
UCLASS(ClassGroup=(YUFS), meta=(BlueprintSpawnableComponent))
class YUFS_API UYUFSNpcSuppressionComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UYUFSNpcSuppressionComponent();
	void Observe(float DeltaTime, int32 SimFrame, const FYUFSNPCObservation& Observation);
	bool Execute(float DeltaTime, int32 SimFrame);
	void Cancel();
	bool IsActive() const { return bActive; }
	bool IsSpraying() const { return bSpraying; }
	FVector GetMovementTarget() const { return MovementTarget; }
	UPROPERTY(EditAnywhere, Category="NPC|Suppression") bool bEnabled = true;
	UPROPERTY(EditAnywhere, Category="NPC|Suppression") float DetectionRadiusCm = 1200.f;
	UPROPERTY(EditAnywhere, Category="NPC|Suppression") float MaxAttemptSeconds = 90.f;
	/** Demonstration-only prior knowledge; does not bypass tool sight or route validation. */
	UPROPERTY(EditAnywhere, Category="NPC|Suppression") bool bKnowsScenarioFireLocation = false;
protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	bool Visible(AActor* Target) const;
	bool CanSeePoint(const FVector& Point, AActor* Target) const;
	void Finish(bool bSuccess, FName Reason);
	void UpdateHeldVisual();
	bool ChooseAttackPoint(int32 SimFrame);
	TWeakObjectPtr<AYUFSEvacuationNPC> Npc;
	TWeakObjectPtr<AYUFSFireExtinguisher> Tool;
	TWeakObjectPtr<AYUFSHeterogeneousVolume> Fire;
	FVector FirePoint = FVector::ZeroVector;
	TSet<FName> FinishedFires;
	FVector MovementTarget = FVector::ZeroVector;
	FVector AttackPoint = FVector::ZeroVector;
	float ScanTimer = 0.f;
	float AttemptSeconds = 0.f;
	float UseSeconds = 0.f;
	float LastFireSeenAt = -1000.f;
	float LastToolSeenAt = -1000.f;
	bool bActive = false;
	bool bSpraying = false;
	bool bRetreatReachable = false;
};
