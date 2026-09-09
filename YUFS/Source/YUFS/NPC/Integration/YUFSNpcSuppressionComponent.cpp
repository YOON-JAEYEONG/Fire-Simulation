#include "NPC/Integration/YUFSNpcSuppressionComponent.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Decision/YUFSHumanBehaviorSelectorComponent.h"
#include "NPC/Decision/YUFSIntentComponent.h"
#include "NPC/Decision/YUFSBeliefComponent.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "NPC/Integration/YUFSTeamIntegrationComponent.h"
#include "NPC/Animation/YUFSActionAnimationComponent.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "Core/YUFSObservation.h"
#include "Fire/YUFSFireExtinguisher.h"
#include "Fire/YUFSHeterogeneousVolume.h"
#include "Level/YUFSLevelDataManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "EngineUtils.h"

UYUFSNpcSuppressionComponent::UYUFSNpcSuppressionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UYUFSNpcSuppressionComponent::Visible(AActor* Target) const
{
	if (!Npc.IsValid() || !IsValid(Target)) return false;
	return CanSeePoint(Target->GetActorLocation() + FVector(0, 0, 45), Target);
}

bool UYUFSNpcSuppressionComponent::CanSeePoint(const FVector& Point, AActor* Target) const
{
	if (!Npc.IsValid()) return false;
	const FVector Eye = Npc->GetActorLocation() + FVector(0, 0, 55);
	if (FVector::DistSquared(Eye, Point) > FMath::Square(DetectionRadiusCm)) return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NpcObjectSight), false, Npc.Get());
	// A held tool must not occlude its owner's view of the fire.
	if (Tool.IsValid() && Tool.Get() != Target && Tool->GetOwnerActor() == Npc.Get())
		Params.AddIgnoredActor(Tool.Get());
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(Hit, Eye, Point, ECC_Visibility, Params)
		|| Hit.GetActor() == Target;
}

void UYUFSNpcSuppressionComponent::Observe(float DeltaTime, int32 SimFrame, const FYUFSNPCObservation& Observation)
{
	Npc = Cast<AYUFSEvacuationNPC>(GetOwner());
	if (!Npc.IsValid() || !bEnabled) return;
	if (bActive && Fire.IsValid() && !Fire->IsLocalFireBurning())
	{ Finish(true, TEXT("LocalFireSuppressed")); return; }
	ScanTimer -= DeltaTime;
	if (ScanTimer > 0.f) return;
	ScanTimer = 0.5f + (Npc->GetStableNPCId() % 7) * 0.013f;
	auto* Team = Npc->GetTeamIntegrationComponent();
	if (!Team) return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (!bActive)
	{
		if (Now - LastToolSeenAt > 15.f || (Tool.IsValid() && Tool->GetOwnerActor())) Tool.Reset();
		if (Now - LastFireSeenAt > 15.f || (Fire.IsValid() && FinishedFires.Contains(Fire->GetFName()))) Fire.Reset();
		float BestTool = FLT_MAX, BestFire = FLT_MAX;
		for (TActorIterator<AYUFSFireExtinguisher> It(GetWorld()); It; ++It)
		{
			const float D = FVector::DistSquared(It->GetActorLocation(), Npc->GetActorLocation());
			if (D < BestTool && !It->GetOwnerActor() && It->GetRemainingAgentNormalized() > 0.f && Visible(*It))
			{ Tool = *It; BestTool = D; LastToolSeenAt = Now; }
		}
		for (TActorIterator<AYUFSHeterogeneousVolume> It(GetWorld()); It; ++It)
		{
			FVector Target;
			if (!It->IsLocalFireBurning() || !It->GetInteractionTarget(Target)) continue;
			const float D = FVector::DistSquared(Target, Npc->GetActorLocation());
			if (D < BestFire && !FinishedFires.Contains(It->GetFName())
				&& (bKnowsScenarioFireLocation || CanSeePoint(Target + FVector(0, 0, 45), *It)))
			{ Fire = *It; FirePoint = Target; BestFire = D; LastFireSeenAt = Now; }
		}
	}
	if (Fire.IsValid() && (bKnowsScenarioFireLocation || CanSeePoint(FirePoint + FVector(0, 0, 45), Fire.Get()))) LastFireSeenAt = Now;
	// A safe exit must already be part of this NPC's level knowledge. Also check
	// that it is reachable on the current floor instead of assuming distance = reachability.
	bRetreatReachable = false;
	FVector Exit;
	if (Tool.IsValid() && Fire.IsValid() && Npc->GetLevelDataManager()
		&& Npc->GetLevelDataManager()->TryGetNearestSafeExit(Npc->GetActorLocation(), SimFrame, Exit))
	{
		UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
			GetWorld(), Npc->GetActorLocation(), Exit, Npc.Get());
		bRetreatReachable = Path && Path->IsValid() && !Path->IsPartial();
		if (bRetreatReachable)
		{
			for (const FVector& Point : Path->PathPoints)
				if (Npc->GetLevelDataManager()->IsLocationDangerous(Point, SimFrame)) bRetreatReachable = false;
		}
	}
	auto Next = Team->GetInteractionOpportunities();
	const auto Old = Next;
	Next.bHoldingExtinguisher = Tool.IsValid() && Tool->GetOwnerActor() == Npc.Get()
		&& Tool->GetExtinguisherState() != EYUFSFireExtinguisherState::Reserved;
	Next.bExtinguisherKnownAvailable = Tool.IsValid() && Tool->GetRemainingAgentNormalized() > 0.f
		&& (!Tool->GetOwnerActor() || Tool->GetOwnerActor() == Npc.Get());
	Next.ExtinguisherStableId = Tool.IsValid() ? Tool->GetFName() : NAME_None;
	Next.ExtinguisherLocation = Tool.IsValid() ? Tool->GetActorLocation() : FVector::ZeroVector;
	Next.bSuppressibleFireKnown = Fire.IsValid() && Fire->IsLocalFireBurning() && Fire->bHasInteractionTarget
		&& !FinishedFires.Contains(Fire->GetFName()) && Now - LastFireSeenAt <= 15.f;
	Next.FireStableId = Fire.IsValid() ? Fire->GetFName() : NAME_None;
	Next.FireLocation = Fire.IsValid() ? FirePoint : FVector::ZeroVector;
	Next.bSafeRetreatKnown = bRetreatReachable && Observation.RiskLevel < 0.65f
		&& Observation.CurrentState != EYUFSBehaviorState::Crawling
		&& Observation.CurrentState != EYUFSBehaviorState::Incapacitated;
	if (Old.ExtinguisherStableId != Next.ExtinguisherStableId || Old.FireStableId != Next.FireStableId
		|| Old.bHoldingExtinguisher != Next.bHoldingExtinguisher
		|| Old.bExtinguisherKnownAvailable != Next.bExtinguisherKnownAvailable
		|| Old.bSuppressibleFireKnown != Next.bSuppressibleFireKnown || Old.bSafeRetreatKnown != Next.bSafeRetreatKnown)
	{
		++Next.KnowledgeRevision;
		Team->SubmitInteractionOpportunities(Next);
		UE_LOG(LogTemp, Display, TEXT("[NPCSuppression] %s discovered tool=%s fire=%s retreat=%d"),
			*Npc->GetName(), *Next.ExtinguisherStableId.ToString(), *Next.FireStableId.ToString(), Next.bSafeRetreatKnown);
	}
}

bool UYUFSNpcSuppressionComponent::Execute(float DeltaTime, int32 SimFrame)
{
	if (!Npc.IsValid() || !bEnabled) { Cancel(); return false; }
	// Observe may finish the task before the selector updates this frame.
	// Never reacquire a released tool using that stale suppression decision.
	if (Fire.IsValid() && (!Fire->IsLocalFireBurning() || FinishedFires.Contains(Fire->GetFName())))
	{
		if (bActive) Finish(true, TEXT("LocalFireSuppressed"));
		return false;
	}
	if (Npc->GetBehaviorStateMachine()->IsIncapacitated() || Npc->GetBehaviorStateMachine()->IsCrawling()
		|| (Npc->GetBeliefComponent() && (Npc->GetBeliefComponent()->HasImmediateLifeRisk()
			|| Npc->GetBeliefComponent()->HasVerifiedOfficialInstruction())))
	{ Cancel(); return false; }
	auto* Selector = Npc->GetHumanBehaviorSelector();
	if (!Selector || Selector->GetCurrentDecision().Behavior != EYUFSHighLevelBehavior::AttemptSuppression)
	{
		if (bActive && Selector)
			UE_LOG(LogTemp, Display, TEXT("[NPCSuppression] cancellation decision=%s"), *Selector->GetCurrentDecision().Reason.ToString());
		Cancel(); return false;
	}
	if (!Tool.IsValid() || !Fire.IsValid() || !bRetreatReachable)
	{ Finish(false, TEXT("SuppressionTargetUnavailable")); return true; }
	FVector CurrentFirePoint;
	if (!Fire->GetInteractionTarget(CurrentFirePoint) || !CurrentFirePoint.Equals(FirePoint, 10.f))
	{ Finish(false, TEXT("ExistingFireTargetChanged")); return true; }
	if (!bActive)
	{
		if (!Tool->TryReserve(Npc.Get())) { Finish(false, TEXT("ToolReservedByAnotherNpc")); return true; }
		bActive = true; AttemptSeconds = 0.f; UseSeconds = 0.f;
		Npc->GetNavigator()->ClearPath();
		UE_LOG(LogTemp, Display, TEXT("[NPCSuppression] SAME actor %s started suppression"), *Npc->GetName());
	}
	AttemptSeconds += DeltaTime;
	if (AttemptSeconds > MaxAttemptSeconds || Tool->GetRemainingAgentNormalized() <= 0.f)
	{ Finish(false, TEXT("SuppressionTimedOutOrEmpty")); return true; }
	const bool Held = Tool->GetExtinguisherState() == EYUFSFireExtinguisherState::Held
		|| Tool->GetExtinguisherState() == EYUFSFireExtinguisherState::Spraying;
	if (!Held)
	{
		MovementTarget = Tool->GetActorLocation();
		if (FVector::Dist2D(Npc->GetActorLocation(), MovementTarget) < 100.f
			&& FMath::Abs(Npc->GetActorLocation().Z - MovementTarget.Z) < 160.f && Visible(Tool.Get()))
		{
			if (!Tool->PickUp(Npc.Get(), Npc->GetRootComponent(), NAME_None))
			{ Finish(false, TEXT("PickupFailed")); return true; }
			Tool->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			if (!ChooseAttackPoint(SimFrame)) { Finish(false, TEXT("NoReachableSuppressionPosition")); return true; }
			MovementTarget = AttackPoint;
			UE_LOG(LogTemp, Display, TEXT("[NPCSuppression] SAME actor %s picked up %s"), *Npc->GetName(), *Tool->GetName());
			ScanTimer = 0.f;
			Npc->GetNavigator()->ClearPath();
		}
	}
	else
	{
		UpdateHeldVisual();
		MovementTarget = AttackPoint;
		const float Distance = FVector::Dist2D(Npc->GetActorLocation(), FirePoint);
		if (Distance >= 140.f && Distance <= 300.f
			&& FMath::Abs(Npc->GetActorLocation().Z - FirePoint.Z) < 160.f && CanSeePoint(FirePoint + FVector(0, 0, 45), Fire.Get()))
		{
			Npc->GetCharacterMovement()->StopMovementImmediately();
			Npc->GetNavigator()->ClearPath();
			const FRotator Aim(0, (FirePoint - Npc->GetActorLocation()).Rotation().Yaw, 0);
			Npc->SetActorRotation(FMath::RInterpConstantTo(Npc->GetActorRotation(), Aim, DeltaTime, 120.f));
			if (FMath::Abs(FMath::FindDeltaAngleDegrees(Npc->GetActorRotation().Yaw, Aim.Yaw)) > 8.f) return true;
			UpdateHeldVisual();
			if (!Npc->bUseExternalMotionDriver)
				Npc->GetActionAnimationComponent()->ApplyAction(EYUFSAction::GatherBelongings, EYUFSBehaviorState::Normal);
			UseSeconds += DeltaTime;
			if (UseSeconds >= 1.5f && Tool->StartSpraying(Npc.Get()))
			{
				if (!bSpraying) UE_LOG(LogTemp, Display, TEXT("[NPCSuppression] SAME actor %s spraying %s"), *Npc->GetName(), *Fire->GetName());
				bSpraying = true;
				const float Used = Tool->ConsumeAgent(Npc.Get(), DeltaTime * 0.75f);
				Fire->ApplyLocalSuppression(Used * 0.25f);
				// Only the local presentation effect responds. Recorded VDB data is unchanged.
				Tool->ShowSprayToward(FirePoint + FVector(0, 0, 25));
				if (UseSeconds >= 7.5f) { Finish(true, TEXT("SuppressionActionCompleted")); return true; }
			}
			return true;
		}
	}
	Tool->StopSpraying(Npc.Get()); bSpraying = false;
	if (!Npc->bUseExternalMotionDriver)
		Npc->GetActionAnimationComponent()->ApplyAction(EYUFSAction::HelpOther, EYUFSBehaviorState::Normal);
	// The character owns actual steering; use the same smoke-aware navigator.
	return false;
}

void UYUFSNpcSuppressionComponent::UpdateHeldVisual()
{
	auto* Mesh = Npc->GetMesh();
	FName Hand = NAME_None;
	for (FName Candidate : {FName(TEXT("RightHand")), FName(TEXT("mixamorig_RightHand")), FName(TEXT("hand_r"))})
		if (Mesh->GetBoneIndex(Candidate) != INDEX_NONE || Mesh->DoesSocketExist(Candidate)) { Hand = Candidate; break; }
	const FVector Grip = Hand.IsNone() ? Npc->GetActorLocation() : Mesh->GetSocketLocation(Hand);
	Tool->SetActorLocationAndRotation(Grip - FVector(0, 0, 43), FRotator(0, Npc->GetActorRotation().Yaw - 90.f, 0));
}

bool UYUFSNpcSuppressionComponent::ChooseAttackPoint(int32 SimFrame)
{
	auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Nav) return false;
	float BestLength = FLT_MAX;
	for (int32 Index = 0; Index < 12; ++Index)
	{
		const float Angle = 2.f * PI * Index / 12.f;
		FNavLocation Projected;
		const FVector Candidate = FirePoint + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * 230.f;
		if (!Nav->ProjectPointToNavigation(Candidate, Projected, FVector(70, 70, 100))) continue;
		if (FMath::Abs(Projected.Location.Z - FirePoint.Z) > 100.f) continue;
		if (Npc->GetLevelDataManager()->IsLocationDangerous(Projected.Location, SimFrame)) continue;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(NpcSuppressionAim), false, Npc.Get());
		Params.AddIgnoredActor(Tool.Get());
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Projected.Location + FVector(0, 0, 100),
			FirePoint + FVector(0, 0, 45), ECC_Visibility, Params) && Hit.GetActor() != Fire.Get()) continue;
		UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
			GetWorld(), Npc->GetActorLocation(), Projected.Location, Npc.Get());
		if (Path && Path->IsValid() && !Path->IsPartial() && Path->GetPathLength() < BestLength)
		{ BestLength = Path->GetPathLength(); AttackPoint = Projected.Location; }
	}
	return BestLength < FLT_MAX;
}

void UYUFSNpcSuppressionComponent::Finish(bool bSuccess, FName Reason)
{
	if (!Npc.IsValid()) return;
	if (Fire.IsValid()) FinishedFires.Add(Fire->GetFName());
	if (Tool.IsValid() && Tool->GetOwnerActor() == Npc.Get())
	{
		Tool->StopSpraying(Npc.Get());
		Tool->SetActorLocation(Npc->GetActorLocation() + Npc->GetActorRightVector() * 65.f - FVector(0, 0, 88));
		Tool->Release(Npc.Get());
	}
	bActive = false; bSpraying = false; ScanTimer = 0.f;
	Npc->GetNavigator()->ClearPath();
	auto* Team = Npc->GetTeamIntegrationComponent();
	auto Snapshot = Team->GetInteractionOpportunities();
	Snapshot.bSuppressibleFireKnown = false; Snapshot.bHoldingExtinguisher = false; ++Snapshot.KnowledgeRevision;
	Team->SubmitInteractionOpportunities(Snapshot);
	FYUFSTeamRequestFeedback Feedback;
	Feedback.RequestRevision = Team->GetInteractionDirective().Revision;
	Feedback.Status = bSuccess ? EYUFSTeamRequestStatus::Completed : EYUFSTeamRequestStatus::Failed;
	Feedback.Reason = Reason;
	Team->SubmitInteractionFeedback(Feedback);
	Npc->GetHumanBehaviorSelector()->RequestReselection(Reason);
	Npc->GetIntentComponent()->ResumeEvacuationAfterInteraction(bRetreatReachable);
	UE_LOG(LogTemp, Display, TEXT("[NPCSuppression] SAME actor %s resumes decision: %s"), *Npc->GetName(), *Reason.ToString());
}

void UYUFSNpcSuppressionComponent::Cancel()
{
	if (bActive) Finish(false, TEXT("SuppressionInterrupted"));
}

void UYUFSNpcSuppressionComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	Cancel();
	Super::EndPlay(Reason);
}
