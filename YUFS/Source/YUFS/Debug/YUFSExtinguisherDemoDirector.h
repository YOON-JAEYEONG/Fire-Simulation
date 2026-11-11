#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NPC/Cognition/YUFSHumanCognitionTypes.h"
#include "NPC/Integration/YUFSTeamIntegrationTypes.h"
#include "YUFSExtinguisherDemoDirector.generated.h"

class ACameraActor;
class AYUFSEvacuationNPC;
class AYUFSFireExtinguisher;
class AYUFSSuppressibleFireSource;
class UMaterialInstanceDynamic;
class USceneComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UYUFSTeamIntegrationComponent;

UENUM()
enum class EYUFSExtinguisherDemoPhase : uint8
{
	Setup,
	MovingToExtinguisher,
	PickingUp,
	MovingToFire,
	Suppressing,
	Succeeded,
	Failed
};

/**
 * Opt-in standalone demonstration of the team-integration contract.
 * Start with -YUFSExtinguisherDemo; production maps are untouched.
 */
UCLASS()
class YUFS_API AYUFSExtinguisherDemoDirector : public AActor
{
	GENERATED_BODY()

public:
	AYUFSExtinguisherDemoDirector();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:
	void SetupDemo();
	void PublishSuppressionDecision();
	void EnterPhase(EYUFSExtinguisherDemoPhase NewPhase, const FString& Message);
	void ApplyPhaseAnimation();
	void CopyNpcAppearance(const AYUFSEvacuationNPC* SourceNpc, AYUFSEvacuationNPC* TargetNpc) const;
	void MoveNpcToward(const FVector& Destination, float DeltaTime, float SpeedCmPerSecond);
	void UpdateHeldExtinguisherTransform();
	void AcquireExtinguisher();
	void BeginSuppression();
	void FinishSuppression();
	void UpdateCamera();
	void UpdateSprayBeam();
	void UpdateScreenStatus() const;
	FString GetPhaseName() const;

	UFUNCTION()
	void OnNavigationDirective(const FYUFSNavigationDirective& Directive);

	UFUNCTION()
	void OnMotionDirective(const FYUFSMotionDirective& Directive);

	UFUNCTION()
	void OnInteractionDirective(const FYUFSInteractionDirective& Directive);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> StageFloor;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> SprayBeam;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> DemoTitle;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FloorMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SprayMaterial;

	UPROPERTY(Transient)
	TObjectPtr<AYUFSEvacuationNPC> DemoNpc;

	UPROPERTY(Transient)
	TObjectPtr<AYUFSFireExtinguisher> Extinguisher;

	UPROPERTY(Transient)
	TObjectPtr<AYUFSSuppressibleFireSource> FireSource;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> DemoCamera;

	UPROPERTY(Transient)
	TObjectPtr<UYUFSTeamIntegrationComponent> TeamIntegration;

	EYUFSExtinguisherDemoPhase Phase = EYUFSExtinguisherDemoPhase::Setup;
	FYUFSBehaviorDecision SuppressionDecision;
	FYUFSInteractionOpportunitySnapshot Opportunities;
	FYUFSCognitiveState DemoCognition;
	FYUFSNavigationDirective LastNavigationDirective;
	FYUFSMotionDirective LastMotionDirective;
	FYUFSInteractionDirective LastInteractionDirective;
	FVector StageOrigin = FVector::ZeroVector;
	FVector ExtinguisherUsePoint = FVector::ZeroVector;
	FVector FireAttackPoint = FVector::ZeroVector;
	FString StatusMessage;
	bool bCameraAssigned = false;
	bool bBuildingMode = false;
	bool bCaptureRequested = false;
	float PhaseElapsedSeconds = 0.f;
};
