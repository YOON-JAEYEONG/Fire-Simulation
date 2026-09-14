#include "NPC/Integration/YUFSNpcSuppressionComponent.h"
#include "NPC/Integration/YUFSSuppressionSafety.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Cognition/YUFSHumanCognitionComponent.h"
#include "NPC/Decision/YUFSHumanBehaviorSelectorComponent.h"
#include "NPC/Decision/YUFSIntentComponent.h"
#include "NPC/Decision/YUFSBeliefComponent.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "NPC/Behavior/YUFSBehaviorConfig.h"
#include "NPC/Integration/YUFSTeamIntegrationComponent.h"
#include "NPC/Animation/YUFSActionAnimationComponent.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "NPC/Perception/YUFSNPCPerceptionComponent.h"
#include "Fire/YUFSFireExtinguisher.h"
#include "Fire/YUFSHeterogeneousVolume.h"
#include "Fire/YUFSBinaryManager.h"
#include "Level/YUFSLevelDataManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "EngineUtils.h"

UYUFSNpcSuppressionComponent::UYUFSNpcSuppressionComponent() { PrimaryComponentTick.bCanEverTick = false; }

bool UYUFSNpcSuppressionComponent::Visible(AActor* Target) const
{
	return Npc.IsValid() && IsValid(Target) && CanSeePoint(Target->GetActorLocation() + FVector(0,0,45), Target);
}
bool UYUFSNpcSuppressionComponent::CanSeePoint(const FVector& Point, AActor* Target) const
{
	if (!Npc.IsValid()) return false;
	const FVector Eye = Npc->GetPawnViewLocation();
	if (FVector::DistSquared(Eye, Point) > FMath::Square(DetectionRadiusCm)) return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(NpcObjectSight), false, Npc.Get());
	if (Tool.IsValid()) Params.AddIgnoredActor(Tool.Get());
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(Hit, Eye, Point, ECC_Visibility, Params) || Hit.GetActor() == Target;
}
void UYUFSNpcSuppressionComponent::Observe(float DeltaTime, int32 SimFrame, const FYUFSNPCObservation& Observation)
{
	Npc = Cast<AYUFSEvacuationNPC>(GetOwner());
	LatestObservation = Observation;
	if (!Npc.IsValid() || !bEnabled) return;
	const bool bDatasetAligned = IsValid(Npc->GetBinaryManager()) && Npc->GetBinaryManager()->IsDatasetAlignmentConfirmed();
	if (Observation.bHazardSampleAvailable && bDatasetAligned) LastHazardSampleAt = GetWorld()->GetTimeSeconds();
	ScanTimer -= DeltaTime;
	if (ScanTimer > 0.f) return;
	ScanTimer = 0.5f + (Npc->GetStableNPCId() % 7) * 0.013f;
	auto* Team = Npc->GetTeamIntegrationComponent();
	if (!Team) return;
	const float Now = GetWorld()->GetTimeSeconds();
	auto InFieldOfView = [this](FVector Point)
	{
		const FVector Direction = (Point - Npc->GetPawnViewLocation()).GetSafeNormal2D();
		return FVector::DotProduct(Npc->GetActorForwardVector().GetSafeNormal2D(), Direction) >= 0.5f;
	};
	if (!bActive)
	{
		if (Now - LastToolSeenAt > 15.f || (Tool.IsValid() && Tool->GetOwnerActor())) Tool.Reset();
		if (Now - LastFireSeenAt > 15.f || (Fire.IsValid() && FinishedFires.Contains(Fire->GetFName()))) Fire.Reset();
		float BestTool = FLT_MAX, BestFire = FLT_MAX;
		for (TActorIterator<AYUFSFireExtinguisher> It(GetWorld()); It; ++It)
		{
			const float D = FVector::DistSquared(It->GetActorLocation(), Npc->GetActorLocation());
			if (D < BestTool && !It->GetOwnerActor() && It->GetRemainingAgentNormalized() > 0.f
				&& InFieldOfView(It->GetActorLocation()) && Visible(*It))
			{ Tool = *It; BestTool = D; LastToolSeenAt = Now; }
		}
		for (TActorIterator<AYUFSHeterogeneousVolume> It(GetWorld()); It; ++It)
		{
			FVector Target;
			if (!bDatasetAligned || !It->IsFdsFireActive() || !It->GetInteractionTarget(Target)) continue;
			const float D = FVector::DistSquared(Target, Npc->GetActorLocation());
			float Smoke = 0.f, Heat = 0.f;
			// Metadata describes reality, not NPC knowledge. Require visible local evidence at the source.
			if (D < BestFire && !FinishedFires.Contains(It->GetFName()) && InFieldOfView(Target)
				&& CanSeePoint(Target + FVector(0,0,45), *It)
				&& Npc->GetNPCPerceptionComponent()->SampleObservedHazard(Target + FVector(0,0,45), SimFrame, Smoke, Heat)
				&& (Heat >= 0.20f || Smoke >= 0.15f))
			{ Fire = *It; FirePoint = Target; BestFire = D; LastFireSeenAt = Now; }
		}
	}
	if (bDatasetAligned && Fire.IsValid() && InFieldOfView(FirePoint) && CanSeePoint(FirePoint + FVector(0,0,45), Fire.Get()))
	{
		float Smoke = 0.f, Heat = 0.f;
		if (Npc->GetNPCPerceptionComponent()->SampleObservedHazard(FirePoint + FVector(0,0,45), SimFrame, Smoke, Heat)
			&& (Heat >= 0.20f || Smoke >= 0.15f)) LastFireSeenAt = Now;
	}
	bRetreatReachable = Tool.IsValid() && Fire.IsValid() && CheckRetreat(SimFrame);
	auto Next = Team->GetInteractionOpportunities();
	const auto Old = Next;
	Next.bHoldingExtinguisher = Tool.IsValid() && Tool->GetOwnerActor() == Npc.Get()
		&& Tool->GetExtinguisherState() != EYUFSFireExtinguisherState::Reserved;
	Next.bExtinguisherKnownAvailable = Tool.IsValid() && Tool->GetRemainingAgentNormalized() > 0.f
		&& (!Tool->GetOwnerActor() || Tool->GetOwnerActor() == Npc.Get());
	Next.ExtinguisherStableId = Tool.IsValid() ? Tool->GetFName() : NAME_None;
	Next.ExtinguisherLocation = Tool.IsValid() ? Tool->GetActorLocation() : FVector::ZeroVector;
	Next.bSuppressibleFireKnown = Fire.IsValid() && Fire->IsFdsFireActive()
		&& !FinishedFires.Contains(Fire->GetFName()) && Now - LastFireSeenAt <= 15.f
		&& Observation.bHazardSampleAvailable && bDatasetAligned;
	Next.FireStableId = Fire.IsValid() ? Fire->GetFName() : NAME_None;
	Next.FireLocation = Fire.IsValid() ? FirePoint : FVector::ZeroVector;
	Next.bSuppressionApproachKnown = bHasAttackPoint && bActive && Next.bHoldingExtinguisher;
	Next.SuppressionApproachLocation = Next.bSuppressionApproachKnown ? AttackPoint : FVector::ZeroVector;
	// "Known retreat" means reachable and not contradicted by observed hazards, not omniscient safety.
	Next.bSafeRetreatKnown = bRetreatReachable && Observation.bHazardSampleAvailable && bDatasetAligned;
	if (Old.ExtinguisherStableId != Next.ExtinguisherStableId || Old.FireStableId != Next.FireStableId
		|| Old.bHoldingExtinguisher != Next.bHoldingExtinguisher || Old.bExtinguisherKnownAvailable != Next.bExtinguisherKnownAvailable
		|| Old.bSuppressibleFireKnown != Next.bSuppressibleFireKnown || Old.bSafeRetreatKnown != Next.bSafeRetreatKnown
		|| Old.bSuppressionApproachKnown != Next.bSuppressionApproachKnown
		|| !Old.SuppressionApproachLocation.Equals(Next.SuppressionApproachLocation, 1.f))
	{
		++Next.KnowledgeRevision;
		Team->SubmitInteractionOpportunities(Next);
		UE_LOG(LogTemp, Display, TEXT("[NPCSuppression] %s observed tool=%s FDS-source=%s retreat=%d data=%d"),
			*Npc->GetName(), *Next.ExtinguisherStableId.ToString(), *Next.FireStableId.ToString(), bRetreatReachable, Observation.bHazardSampleAvailable);
	}
}
bool UYUFSNpcSuppressionComponent::CheckRetreat(int32 SimFrame)
{
	RetreatPath.Reset();
	RetreatExit = FVector::ZeroVector;
	if (!Npc.IsValid() || !Npc->GetLevelDataManager() || !Npc->GetNavigator()) return false;
	float BestLength = FLT_MAX;
	for (const FVector& Exit : Npc->GetLevelDataManager()->GetKnownExitLocations())
	{
		auto* Path = UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(), Npc->GetActorLocation(), Exit, Npc.Get());
		if (Path && Path->IsValid() && !Path->IsPartial() && !Path->PathPoints.IsEmpty()
			&& !Npc->GetNavigator()->IsKnownPathDangerous(Path->PathPoints) && Path->GetPathLength() < BestLength)
		{
			BestLength = Path->GetPathLength();
			RetreatExit = Exit;
			RetreatPath = Path->PathPoints;
		}
	}
	return !RetreatPath.IsEmpty();
}
bool UYUFSNpcSuppressionComponent::ReassessSafety(int32 SimFrame)
{
	if (!bActive || !Npc.IsValid()) return false;
	const auto* Cognition = Npc->GetHumanCognitionComponent();
	const auto* State = Npc->GetBehaviorStateMachine();
	if (!bEnabled || !Cognition || !State) { Finish(false, TEXT("InteractionExecutorDisabled")); return true; }
	const auto& C = Cognition->GetCognitiveState();
	auto O = LatestObservation;
	O.CurrentState = State->GetCurrentState();
	O.RiskPerception = State->GetRiskPerception();
	O.bSuppressionAllowedByBehavior = Npc->AllowsOptionalInteractions();
	const auto& Traits = Cognition->GetTraits();
	FName Reason = NAME_None;
	if (!FYUFSSuppressionSafety::CanAttempt(O, C, Traits, true))
	{
		const float SmokeLimit = State->Config ? State->Config->SmokeAwarenessThreshold * State->Config->EmergencyOverrideMultiplier : 0.30f;
		const float HeatLimit = State->Config ? State->Config->EmergencyHeatThreshold : 0.65f;
		Reason = FYUFSSuppressionSafety::ImmediateDanger(O, SmokeLimit, HeatLimit) ? TEXT("ImmediateObservedDanger")
			: !O.bSuppressionAllowedByBehavior ? TEXT("JjwBehaviorPriority") : TEXT("PerceivedRiskOrEvacuationPriority");
	}
	else if (!IsValid(Npc->GetBinaryManager()) || !Npc->GetBinaryManager()->IsDatasetAlignmentConfirmed()) Reason = TEXT("UnconfirmedHazardAlignment");
	else if (!Fire.IsValid() || !Fire->IsFdsFireActive()) Reason = TEXT("FdsSourceUnavailable");
	else if (!Tool.IsValid() || Tool->GetOwnerActor() != Npc.Get()) Reason = TEXT("ToolOwnershipLost");
	else if (GetWorld()->GetTimeSeconds() - LastHazardSampleAt > 1.f) Reason = TEXT("HazardDataUnavailable");
	else if (!bRetreatReachable) Reason = TEXT("KnownRetreatLost");
	if (Reason.IsNone()) return false;
	UE_LOG(LogTemp, Display, TEXT("[NPCSuppression] %s abort=%s perceivedRisk=%.3f stopAt=%.3f heatNear=%.3f"),
		*Npc->GetName(), *Reason.ToString(), FYUFSSuppressionSafety::PerceivedRisk(O,C), FYUFSSuppressionSafety::StopRisk(Traits), O.NearbyHeatNormalized);
	Finish(false, Reason);
	return true;
}
void UYUFSNpcSuppressionComponent::UpdatePresentation()
{
	if (bActive && Tool.IsValid() && Tool->GetOwnerActor() == Npc.Get()
		&& Tool->GetExtinguisherState() != EYUFSFireExtinguisherState::Reserved) UpdateHeldVisual();
}
bool UYUFSNpcSuppressionComponent::Execute(float DeltaTime, int32 SimFrame)
{
	if (!Npc.IsValid() || !bEnabled) { Cancel(); return false; }
	if (ReassessSafety(SimFrame)) return true;
	const auto* TeamDecision = Npc->GetTeamIntegrationComponent();
	if (!TeamDecision || (TeamDecision->GetInteractionDirective().Goal != EYUFSInteractionGoal::AcquireExtinguisher
		&& TeamDecision->GetInteractionDirective().Goal != EYUFSInteractionGoal::SuppressFire
		&& !(bActive && TeamDecision->GetInteractionDirective().Goal == EYUFSInteractionGoal::OpenDoor)))
	{
		// Only the decision actually published by the JJW bridge may execute. A retained
		// selector proposal is not permission after the feature or decision was withdrawn.
		Cancel(); return false;
	}
	// Never let stale selector/directive state reserve a tool before JJW has committed,
	// during preparation, or after its current physical emergency gate takes priority.
	auto CurrentObservation = LatestObservation;
	const auto* State = Npc->GetBehaviorStateMachine();
	const auto* Cognition = Npc->GetHumanCognitionComponent();
	if (!State || !Cognition || !Npc->AllowsOptionalInteractions()) { Cancel(); return false; }
	CurrentObservation.CurrentState = State->GetCurrentState();
	CurrentObservation.RiskPerception = State->GetRiskPerception();
	CurrentObservation.bSuppressionAllowedByBehavior = true;
	if (!FYUFSSuppressionSafety::CanAttempt(CurrentObservation, Cognition->GetCognitiveState(), Cognition->GetTraits(), bActive))
	{ Cancel(); return false; }
	if (Fire.IsValid() && FinishedFires.Contains(Fire->GetFName())) return false;
	auto* Selector = Npc->GetHumanBehaviorSelector();
	if (!Selector || Selector->GetCurrentDecision().Behavior != EYUFSHighLevelBehavior::AttemptSuppression)
	{ Cancel(); return false; }
	if (!Tool.IsValid() || !Fire.IsValid() || !Fire->IsFdsFireActive() || !bRetreatReachable
		|| !LatestObservation.bHazardSampleAvailable || !IsValid(Npc->GetBinaryManager())
		|| !Npc->GetBinaryManager()->IsDatasetAlignmentConfirmed())
	{ Finish(false, TEXT("AttemptPrerequisiteUnavailable")); return true; }
	FVector CurrentFirePoint;
	if (!Fire->GetInteractionTarget(CurrentFirePoint) || !CurrentFirePoint.Equals(FirePoint, 10.f))
	{ Finish(false, TEXT("FdsSourceChanged")); return true; }
	if (!bActive)
	{
		if (!Tool->TryReserve(Npc.Get())) { Finish(false, TEXT("ToolReservedByAnotherNpc")); return true; }
		bActive = true; AttemptSeconds = UseSeconds = 0.f;
		ExecutionRevision = Npc->GetTeamIntegrationComponent()->GetInteractionDirective().Revision;
		Npc->GetNavigator()->ClearPath();
		UE_LOG(LogTemp, Display, TEXT("[NPCSuppression] %s attempts FDS source %s at %s; no fire mutation"),
			*Npc->GetName(), *Fire->GetName(), *FirePoint.ToCompactString());
	}
	const auto& Directive = Npc->GetTeamIntegrationComponent()->GetInteractionDirective();
	if (Directive.Goal == EYUFSInteractionGoal::SuppressFire || Directive.Goal == EYUFSInteractionGoal::AcquireExtinguisher)
		ExecutionRevision = Directive.Revision;
	AttemptSeconds += FMath::Max(0.f, DeltaTime);
	if (AttemptSeconds >= MaxAttemptSeconds) { Finish(false, TEXT("AttemptTimeBudgetReached")); return true; }
	if (Tool->GetRemainingAgentNormalized() <= 0.f) { Finish(false, TEXT("ExtinguisherEmptyFireUnchanged")); return true; }
	const bool Held = Tool->GetExtinguisherState() == EYUFSFireExtinguisherState::Held
		|| Tool->GetExtinguisherState() == EYUFSFireExtinguisherState::Spraying;
	if (!Held)
	{
		MovementTarget = Tool->GetActorLocation();
		if (FVector::Dist2D(Npc->GetActorLocation(), MovementTarget) < 100.f
			&& FMath::Abs(Npc->GetActorLocation().Z - MovementTarget.Z) < 160.f && Visible(Tool.Get()))
		{
			if (!Tool->PickUp(Npc.Get(), Npc->GetRootComponent(), NAME_None)) { Finish(false, TEXT("PickupFailed")); return true; }
			Tool->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			if (!ChooseAttackPoint(SimFrame)) { Finish(false, TEXT("NoReachableObservedAttackPosition")); return true; }
			bHasAttackPoint = true;
			auto Snapshot = Npc->GetTeamIntegrationComponent()->GetInteractionOpportunities();
			Snapshot.bHoldingExtinguisher = true;
			Snapshot.bSuppressionApproachKnown = true;
			Snapshot.SuppressionApproachLocation = AttackPoint;
			++Snapshot.KnowledgeRevision;
			Npc->GetTeamIntegrationComponent()->SubmitInteractionOpportunities(Snapshot);
			MovementTarget = AttackPoint; ScanTimer = 0.f;
			Npc->GetNavigator()->ClearPath();
		}
	}
	else
	{
		UpdateHeldVisual();
		MovementTarget = AttackPoint;
		const float Distance = FVector::Dist2D(Npc->GetActorLocation(), FirePoint);
		if (FVector::Dist2D(Npc->GetActorLocation(), AttackPoint) <= 65.f && Distance >= 140.f && Distance <= 300.f
			&& FMath::Abs(Npc->GetActorLocation().Z - FirePoint.Z) < 160.f
			&& CanSeePoint(FirePoint + FVector(0,0,45), Fire.Get()))
		{
			Npc->GetCharacterMovement()->StopMovementImmediately();
			Npc->GetNavigator()->ClearPath();
			const FRotator Aim(0, (FirePoint - Npc->GetActorLocation()).Rotation().Yaw, 0);
			Npc->SetActorRotation(FMath::RInterpConstantTo(Npc->GetActorRotation(), Aim, DeltaTime, 120.f));
			if (FMath::Abs(FMath::FindDeltaAngleDegrees(Npc->GetActorRotation().Yaw, Aim.Yaw)) > 8.f) return true;
			UpdateHeldVisual();
			if (!Npc->bUseExternalMotionDriver)
				Npc->GetActionAnimationComponent()->ApplyAction(EYUFSAction::GatherBelongings, EYUFSBehaviorState::Normal);
			UseSeconds += FMath::Max(0.f, DeltaTime);
			if (UseSeconds >= 1.5f && Tool->StartSpraying(Npc.Get()))
			{
				if (!bSpraying) UE_LOG(LogTemp, Display, TEXT("[NPCSuppression] %s spraying gesture, FDS data unchanged"), *Npc->GetName());
				bSpraying = true;
				Tool->ConsumeAgent(Npc.Get(), DeltaTime * 0.75f);
				Tool->ShowSprayToward(FirePoint + FVector(0,0,25));
				// No damage, strength reduction, extinguished event, or timed success.
			}
			return true;
		}
	}
	Tool->StopSpraying(Npc.Get()); bSpraying = false; UseSeconds = 0.f;
	if (!Npc->bUseExternalMotionDriver)
		Npc->GetActionAnimationComponent()->ApplyAction(EYUFSAction::HelpOther, EYUFSBehaviorState::Normal);
	return false; // The same character and navigator own all locomotion.
}
void UYUFSNpcSuppressionComponent::UpdateHeldVisual()
{
	auto* Mesh = Npc->GetMesh();
	if (!Mesh || !Tool.IsValid()) return;
	FName Hand = NAME_None;
	for (FName Candidate : {FName(TEXT("RightHand")), FName(TEXT("mixamorig_RightHand")), FName(TEXT("hand_r"))})
		if (Mesh->GetBoneIndex(Candidate) != INDEX_NONE || Mesh->DoesSocketExist(Candidate)) { Hand = Candidate; break; }
	const FVector Grip = Hand.IsNone() ? Npc->GetActorLocation() : Mesh->GetSocketLocation(Hand);
	Tool->SetActorLocationAndRotation(Grip - FVector(0,0,43), FRotator(0, Npc->GetActorRotation().Yaw - 90.f, 0));
}
bool UYUFSNpcSuppressionComponent::ChooseAttackPoint(int32 SimFrame)
{
	auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Nav || !Npc->GetNavigator()) return false;
	float BestLength = FLT_MAX;
	for (int32 Index = 0; Index < 12; ++Index)
	{
		const float Angle = 2.f * PI * Index / 12.f;
		FNavLocation Projected;
		const FVector Candidate = FirePoint + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * 230.f;
		if (!Nav->ProjectPointToNavigation(Candidate, Projected, FVector(70,70,100))) continue;
		if (FMath::Abs(Projected.Location.Z - FirePoint.Z) > 100.f
			|| Npc->GetNavigator()->IsKnownLocationDangerous(Projected.Location + FVector(0,0,120))) continue;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(NpcSuppressionAim), false, Npc.Get());
		Params.AddIgnoredActor(Tool.Get());
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Projected.Location + FVector(0,0,100),
			FirePoint + FVector(0,0,45), ECC_Visibility, Params) && Hit.GetActor() != Fire.Get()) continue;
		auto* Path = UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(), Npc->GetActorLocation(), Projected.Location, Npc.Get());
		if (Path && Path->IsValid() && !Path->IsPartial() && !Path->PathPoints.IsEmpty()
			&& !Npc->GetNavigator()->IsKnownPathDangerous(Path->PathPoints) && Path->GetPathLength() < BestLength)
		{ BestLength = Path->GetPathLength(); AttackPoint = Projected.Location; }
	}
	return BestLength < FLT_MAX;
}
void UYUFSNpcSuppressionComponent::Finish(bool bSuccess, FName Reason)
{
	LastStopReason = Reason;
	if (!Npc.IsValid()) return;
	if (bActive && !Npc->bUseExternalNavigationDriver)
	{
		auto* Movement = Npc->GetCharacterMovement();
		const auto* State = Npc->GetBehaviorStateMachine();
		Movement->MaxWalkSpeed = Npc->GetDesiredWalkingSpeed();
		if (State && State->IsIncapacitated()) Movement->MaxWalkSpeed = 0.f;
		else if (State && State->IsCrawling() && State->Config)
			Movement->MaxWalkSpeed = FMath::Min(Movement->MaxWalkSpeed, State->Config->CrawlSpeed);
	}
	if (Fire.IsValid()) FinishedFires.Add(Fire->GetFName()); // This NPC has tried, not "the fire was extinguished".
	if (Tool.IsValid() && Tool->GetOwnerActor() == Npc.Get())
	{
		const bool bWasHeld = Tool->GetExtinguisherState() != EYUFSFireExtinguisherState::Reserved;
		Tool->StopSpraying(Npc.Get());
		if (bWasHeld)
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(NpcToolDrop), false, Npc.Get());
			Params.AddIgnoredActor(Tool.Get());
			FHitResult Floor;
			const FVector Drop = Npc->GetActorLocation() + Npc->GetActorRightVector() * 55.f;
			if (GetWorld()->LineTraceSingleByChannel(Floor, Drop, Drop - FVector(0,0,180), ECC_Visibility, Params)
				&& Floor.ImpactNormal.Z > 0.6f) Tool->SetActorLocation(Floor.ImpactPoint + FVector(0,0,3));
		}
		Tool->Release(Npc.Get());
	}
	bActive = bSpraying = bHasAttackPoint = false; ScanTimer = 0.f;
	Npc->GetNavigator()->ClearPath(); // Invalidates late asynchronous approach results.
	auto* Team = Npc->GetTeamIntegrationComponent();
	if (Team)
	{
		auto Snapshot = Team->GetInteractionOpportunities();
		Snapshot.bSuppressibleFireKnown = Snapshot.bHoldingExtinguisher = false; ++Snapshot.KnowledgeRevision;
		Snapshot.bSuppressionApproachKnown = false;
		Snapshot.SuppressionApproachLocation = FVector::ZeroVector;
		Team->SubmitInteractionOpportunities(Snapshot);
		FYUFSTeamRequestFeedback Feedback;
		Feedback.RequestRevision = ExecutionRevision;
		Feedback.Status = EYUFSTeamRequestStatus::Cancelled;
		Feedback.Reason = Reason;
		Team->SubmitInteractionFeedback(Feedback);
	}
	Npc->GetHumanBehaviorSelector()->RequestReselection(Reason);
	if (bResumeOnFinish) Npc->ResumeEvacuationAfterSuppression(bRetreatReachable, RetreatExit, RetreatPath);
	UE_LOG(LogTemp, Display, TEXT("[NPCSuppression] %s stopped attempt: %s; fire remains unchanged"), *Npc->GetName(), *Reason.ToString());
}
void UYUFSNpcSuppressionComponent::Cancel(bool bResumeEvacuation)
{
	TGuardValue<bool> ResumeGuard(bResumeOnFinish, bResumeEvacuation);
	if (bActive) Finish(false, TEXT("SuppressionInterrupted"));
}
void UYUFSNpcSuppressionComponent::ResetForEpisode()
{
	Cancel(false);
	Tool.Reset(); Fire.Reset(); FinishedFires.Reset();
	RetreatPath.Reset(); RetreatExit = FVector::ZeroVector;
	bRetreatReachable = bHasAttackPoint = false;
	LastFireSeenAt = LastToolSeenAt = LastHazardSampleAt = -1000.f;
	ScanTimer = AttemptSeconds = UseSeconds = 0.f;
	LastStopReason = NAME_None;
}
void UYUFSNpcSuppressionComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	Cancel(false);
	Super::EndPlay(Reason);
}
