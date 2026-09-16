#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/YUFSObservation.h"
#include "YUFSNpcSuppressionComponent.generated.h"

class AYUFSEvacuationNPC;
class AYUFSFireExtinguisher;
class AYUFSHeterogeneousVolume;
class ACameraActor;
struct FYUFSNPCObservation;

/** Object interaction fallback on the SAME evacuation character. No extra pawn or teleport. */
UCLASS(ClassGroup=(YUFS), meta=(BlueprintSpawnableComponent))
class YUFS_API UYUFSNpcSuppressionComponent : public UActorComponent
{
	GENERATED_BODY()
	friend class FYUFSSuppressionCancellationIntegrationTest;
public:
	UYUFSNpcSuppressionComponent();
	void Observe(float DeltaTime, int32 SimFrame, const FYUFSNPCObservation& Observation);
	bool Execute(float DeltaTime, int32 SimFrame);
	// Called before door/help execution so an interrupt is never hidden by a sub-action.
	bool ReassessSafety(int32 SimFrame);
	void UpdatePresentation();
	void Cancel(bool bResumeEvacuation = true);
	void ResetForEpisode();
	bool IsActive() const { return bActive; }
	bool IsSpraying() const { return bSpraying; }
	/** Explicit animation preview only: target is authored, never an FDS observation. */
	UFUNCTION(BlueprintCallable, Category="NPC|Suppression|Presentation")
	bool StartVisualPresentation(AYUFSFireExtinguisher* Extinguisher, FVector Target, float DurationSeconds = 8.f);
	bool StartAuthoredApproach(AYUFSFireExtinguisher* Extinguisher, FVector Target, FVector SprayFeet, float DurationSeconds);
	UFUNCTION(BlueprintPure, Category="NPC|Suppression|Presentation")
	bool IsVisualPresentationActive() const { return bVisualPresentation; }
	UFUNCTION(BlueprintCallable, Category="NPC|Suppression|Presentation")
	bool FocusVisualPresentation();
	bool TickVisualPresentation(float DeltaTime);
	bool CanStartAuthoredGesture() const;
	bool ShouldInterruptAuthoredGesture(const FYUFSNPCObservation& Observation) const;
	// Historical diagnostic, separate from revision-scoped team request feedback.
	/** Historical executor outcome; unlike request feedback, survives a new evacuation directive. */
	FName GetLastStopReason() const { return LastStopReason; }
	FVector GetMovementTarget() const { return MovementTarget; }
	UPROPERTY(EditAnywhere, Category="NPC|Suppression") bool bEnabled = true;
	UPROPERTY(EditAnywhere, Category="NPC|Suppression") float DetectionRadiusCm = 1200.f;
	UPROPERTY(EditAnywhere, Category="NPC|Suppression") float MaxAttemptSeconds = 90.f;
	/** Compatibility only. Omniscient scenario knowledge is no longer supported. */
	UPROPERTY(meta=(DeprecatedProperty)) bool bKnowsScenarioFireLocation = false;
protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	bool Visible(AActor* Target) const;
	bool CanSeePoint(const FVector& Point, AActor* Target) const;
	void Finish(bool bSuccess, FName Reason);
	void UpdateHeldVisual();
	bool ChooseAttackPoint(int32 SimFrame);
	bool CheckRetreat(int32 SimFrame);
	TWeakObjectPtr<AYUFSEvacuationNPC> Npc;
	TWeakObjectPtr<AYUFSFireExtinguisher> Tool;
	TWeakObjectPtr<AYUFSHeterogeneousVolume> Fire;
	FVector FirePoint = FVector::ZeroVector;
	TSet<FName> FinishedFires;
	FVector MovementTarget = FVector::ZeroVector;
	FVector AttackPoint = FVector::ZeroVector;
	FVector RetreatExit = FVector::ZeroVector;
	TArray<FVector> RetreatPath;
	FName LastStopReason = NAME_None;
	float ScanTimer = 0.f;
	float AttemptSeconds = 0.f;
	float UseSeconds = 0.f;
	float LastFireSeenAt = -1000.f;
	float LastToolSeenAt = -1000.f;
	float LastHazardSampleAt = -1000.f;
	int64 ExecutionRevision = 0;
	FYUFSNPCObservation LatestObservation;
	bool bActive = false;
	bool bSpraying = false;
	bool bRetreatReachable = false;
	bool bHasAttackPoint = false;
	bool bResumeOnFinish = true;
	void FinishVisualPresentation(FName Reason);
	bool bVisualPresentation = false;
	bool bAuthoredApproach = false;
	FVector PresentationSprayFeet = FVector::ZeroVector;
	bool bPresentationWasLogging = false;
	float PresentationDuration = 8.f;
	float PresentationElapsed = 0.f;
	float PresentationSprayElapsed = 0.f;
	FTransform PresentationToolTransform = FTransform::Identity;
	TWeakObjectPtr<ACameraActor> PresentationCamera;
	TWeakObjectPtr<AActor> PreviousViewTarget;
};
