#include "NPC/Integration/YUFSBelongingsRetrievalComponent.h"

#include "Core/YUFSObservation.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Animation/YUFSActionAnimationComponent.h"
#include "NPC/Behavior/YUFSBehaviorConfig.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "NPC/Decision/YUFSHumanBehaviorSelectorComponent.h"
#include "NPC/Integration/YUFSTeamIntegrationComponent.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "Fire/YUFSHazardField.h"
#include "Level/YUFSLevelDataManager.h"
#include "Props/YUFSBelongingsBag.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
	const FName BelongingsSourceTag(TEXT("YUFS.Belongings"));
}

UYUFSBelongingsRetrievalComponent::UYUFSBelongingsRetrievalComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UYUFSBelongingsRetrievalComponent::BeginPlay()
{
	Super::BeginPlay();
	Npc = Cast<AYUFSEvacuationNPC>(GetOwner());
}

void UYUFSBelongingsRetrievalComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	Cancel(false);
	DestroyBag();
	Super::EndPlay(Reason);
}

void UYUFSBelongingsRetrievalComponent::RollForEpisode()
{
	bRolled = true;
	bWantsBag = false;
	if (!Npc.IsValid()) return;
	// Private stream: the roll never consumes draws from the NPC's decision/route RNG streams.
	FRandomStream Stream(static_cast<int32>(HashCombine(
		GetTypeHash(Npc->ScenarioSeed ^ 0x5BA6),
		HashCombine(GetTypeHash(Npc->GetStableNPCId()), GetTypeHash(EpisodeCounter)))));
	bWantsBag = Stream.FRand() < FMath::Clamp(LeaveBehindProbability, 0.f, 1.f);
	const float MinDelay = FMath::Max(0.f, FMath::Min(RememberDelaySeconds.X, RememberDelaySeconds.Y));
	const float MaxDelay = FMath::Max(MinDelay, FMath::Max(RememberDelaySeconds.X, RememberDelaySeconds.Y));
	RememberDelay = Stream.FRandRange(MinDelay, MaxDelay);
}

FVector UYUFSBelongingsRetrievalComponent::GetEpisodeOrigin() const
{
	if (!Npc.IsValid()) return FVector::ZeroVector;
	return Npc->GetSpawnLocation().IsZero() ? Npc->GetActorLocation() : Npc->GetSpawnLocation();
}

bool UYUFSBelongingsRetrievalComponent::ClaimLevelBag()
{
	UWorld* World = GetWorld();
	if (!Npc.IsValid() || !World) return false;
	const FVector Origin = GetEpisodeOrigin();
	AYUFSBelongingsBag* Best = nullptr;
	float BestDistSq = FMath::Square(LevelBagClaimRadiusCm);
	for (TActorIterator<AYUFSBelongingsBag> It(World); It; ++It)
	{
		AYUFSBelongingsBag* Candidate = *It;
		if (!Candidate->IsLevelPlaced() || Candidate->IsClaimed() || Candidate->IsCarried()) continue;
		// Same floor only: a bag one storey up is not "the bag I left at my desk".
		if (FMath::Abs(Candidate->GetPickupLocation().Z - (Origin.Z - Npc->GetSimpleCollisionHalfHeight())) > 150.f) continue;
		const float DistSq = FVector::DistSquared2D(Candidate->GetActorLocation(), Origin);
		if (DistSq < BestDistSq) { BestDistSq = DistSq; Best = Candidate; }
	}
	if (!Best || !Best->Claim(Npc.Get())) return false;
	Bag = Best;
	Phase = EYUFSBelongingsRetrievalPhase::LeftBehind;
	PublishOpportunity();
	UE_LOG(LogTemp, Display, TEXT("[NPCBelongings] %s owns level bag %s at %s (%.0fcm away); remembers it after %.1fs of evacuation"),
		*Npc->GetName(), *Best->GetName(), *Best->GetActorLocation().ToCompactString(), FMath::Sqrt(BestDistSq), RememberDelay);
	return true;
}

bool UYUFSBelongingsRetrievalComponent::PlaceBag()
{
	UWorld* World = GetWorld();
	if (!Npc.IsValid() || !World) return false;
	const FVector Origin = GetEpisodeOrigin();
	// Beside where the NPC originally stood (desk/seat), on the actual floor.
	const float Yaw = static_cast<float>(Npc->GetStableNPCId() % 360);
	const FVector Candidate = Origin + FRotator(0.f, Yaw, 0.f).Vector() * 45.f;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(YUFSBelongingsFloor), false, Npc.Get());
	FHitResult Floor;
	FVector Ground;
	if (World->LineTraceSingleByChannel(Floor, Candidate + FVector(0, 0, 50), Candidate - FVector(0, 0, 250),
		ECC_Visibility, Params) && Floor.ImpactNormal.Z > 0.6f)
	{
		Ground = Floor.ImpactPoint;
	}
	else if (World->LineTraceSingleByChannel(Floor, Origin, Origin - FVector(0, 0, 250), ECC_Visibility, Params))
	{
		Ground = Floor.ImpactPoint;
	}
	else
	{
		return false;
	}
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Spawn.Owner = Npc.Get();
	auto* NewBag = World->SpawnActor<AYUFSBelongingsBag>(Ground + FVector(0, 0, 20.f), FRotator(0.f, Yaw, 0.f), Spawn);
	if (!NewBag) return false;
	NewBag->AssignOwnerNpc(Npc.Get());
	NewBag->Tags.Add(BelongingsSourceTag);
#if WITH_EDITOR
	NewBag->SetActorLabel(FString::Printf(TEXT("Belongings_%s"), *Npc->GetName()));
#endif
	Bag = NewBag;
	Phase = EYUFSBelongingsRetrievalPhase::LeftBehind;
	PublishOpportunity();
	UE_LOG(LogTemp, Display, TEXT("[NPCBelongings] %s's bag placed at %s; remembers it after %.1fs of evacuation"),
		*Npc->GetName(), *NewBag->GetActorLocation().ToCompactString(), RememberDelay);
	return true;
}

void UYUFSBelongingsRetrievalComponent::PublishOpportunity() const
{
	if (!Npc.IsValid()) return;
	auto* Team = Npc->GetTeamIntegrationComponent();
	if (!Team) return;
	auto Snapshot = Team->GetInteractionOpportunities();
	const bool bKnown = Bag.IsValid() && !Bag->IsCarried() && Phase != EYUFSBelongingsRetrievalPhase::Abandoned;
	Snapshot.bBelongingsKnown = bKnown;
	Snapshot.BelongingsStableId = bKnown ? Bag->GetFName() : NAME_None;
	Snapshot.BelongingsLocation = bKnown ? Bag->GetPickupLocation() : FVector::ZeroVector;
	Snapshot.bCarryingBelongings = Bag.IsValid() && Bag->IsCarried();
	++Snapshot.KnowledgeRevision;
	Team->SubmitInteractionOpportunities(Snapshot);
}

bool UYUFSBelongingsRetrievalComponent::IsReturnSafe(const FYUFSNPCObservation& Observation, FName& OutReason) const
{
	OutReason = NAME_None;
	const auto* State = Npc.IsValid() ? Npc->GetBehaviorStateMachine() : nullptr;
	if (!State) { OutReason = TEXT("NoBehaviorState"); return false; }
	if (State->IsIncapacitated()) { OutReason = TEXT("Incapacitated"); return false; }
	if (State->IsCrawling()) { OutReason = TEXT("Crawling"); return false; }
	const auto* Config = State->Config;
	const float SmokeLimit = Config ? Config->SmokeAwarenessThreshold * Config->EmergencyOverrideMultiplier : 0.30f;
	const float HeatLimit = Config ? Config->EmergencyHeatThreshold : 0.65f;
	if (Observation.SmokeDensityAtSelf > SmokeLimit) { OutReason = TEXT("ObservedSmoke"); return false; }
	if (FMath::Max(Observation.TemperatureAtSelf, Observation.NearbyHeatNormalized) >= HeatLimit)
	{ OutReason = TEXT("ObservedHeat"); return false; }
	if (State->GetRiskPerception() > MaxRiskToReturn) { OutReason = TEXT("PerceivedRiskTooHigh"); return false; }
	if (Observation.bReceivedStaffGuidance || Npc->HasReceivedStaffGuidance())
	{ OutReason = TEXT("StaffGuidance"); return false; }
	return true;
}

bool UYUFSBelongingsRetrievalComponent::IsObservedDangerAlong(const TArray<FVector>& FloorPoints) const
{
	auto* Navigator = Npc.IsValid() ? Npc->GetNavigator() : nullptr;
	if (!Navigator || FloorPoints.IsEmpty()) return false;
	const auto* LevelData = Npc->GetLevelDataManager();
	const int32 Frame = IsValid(LevelData) ? LevelData->GetCurrentHazardFrame() : Npc->GetCurrentSimFrame();
	// Personal knowledge only: cells this NPC has perceived. Unknown is not evidence of danger.
	const FYUFSHazardSnapshot Snapshot = Navigator->GetPerceivedHazardSnapshot(Frame);
	const FVector Height(0.f, 0.f, Navigator->HazardSampleHeightCm);
	auto Dangerous = [&](const FVector& Floor)
	{
		const FYUFSHazardSample Sample = Snapshot.Sample(Floor + Height);
		return Sample.Status == EYUFSHazardDataStatus::Ready
			&& (Sample.Smoke >= Navigator->UnsafeSmokeThreshold || Sample.Heat >= Navigator->UnsafeHeatThreshold);
	};
	constexpr float StepCm = 50.f;
	for (int32 Index = 0; Index < FloorPoints.Num(); ++Index)
	{
		if (Dangerous(FloorPoints[Index])) return true;
		if (Index + 1 >= FloorPoints.Num()) break;
		const FVector Start = FloorPoints[Index], End = FloorPoints[Index + 1];
		const int32 Steps = FMath::Min(200, FMath::FloorToInt(FVector::Dist(Start, End) / StepCm));
		for (int32 Step = 1; Step < Steps; ++Step)
			if (Dangerous(FMath::Lerp(Start, End, static_cast<float>(Step) / Steps))) return true;
	}
	return false;
}

bool UYUFSBelongingsRetrievalComponent::HasSafeReturnPath(FName& OutReason) const
{
	OutReason = NAME_None;
	auto* Navigator = Npc.IsValid() ? Npc->GetNavigator() : nullptr;
	if (!Navigator || !Bag.IsValid()) { OutReason = TEXT("NoNavigator"); return false; }
	const FVector Target = Bag->GetPickupLocation();
	if (IsObservedDangerAlong({ Target })) { OutReason = TEXT("BagAreaObservedDangerous"); return false; }
	auto* Path = UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(), Npc->GetActorLocation(), Target, Npc.Get());
	if (!Path || !Path->IsValid() || Path->IsPartial() || Path->PathPoints.IsEmpty())
	{ OutReason = TEXT("BagUnreachable"); return false; }
	if (Path->GetPathLength() > MaxReturnPathCm) { OutReason = TEXT("BagTooFar"); return false; }
	if (IsObservedDangerAlong(Path->PathPoints)) { OutReason = TEXT("ReturnPathObservedDangerous"); return false; }
	return true;
}

void UYUFSBelongingsRetrievalComponent::PrepareForEpisode()
{
	Npc = Cast<AYUFSEvacuationNPC>(GetOwner());
	if (!Npc.IsValid() || !bEnabled || Npc->bUseExternalNavigationDriver || Npc->IsTimelinePlaybackMode()
		|| !GetWorld() || !GetWorld()->IsGameWorld()) return;
	// Bags are assigned once, when the episode is set up; a bag freed mid-run is not re-assigned.
	const bool bEpisodeSetup = !bRolled;
	if (!bRolled) RollForEpisode();
	if (!bEpisodeSetup || Phase != EYUFSBelongingsRetrievalPhase::None || Bag.IsValid()) return;
	// A designer-placed bag next to this NPC is always its own, regardless of the random roll.
	if (ClaimLevelBag()) { bWantsBag = true; return; }
	bool bLevelHasPlacedBags = false;
	for (TActorIterator<AYUFSBelongingsBag> It(GetWorld()); It && !bLevelHasPlacedBags; ++It)
		bLevelHasPlacedBags = It->IsLevelPlaced();
	if (bWantsBag && (bLevelHasPlacedBags || !PlaceBag()))
		bWantsBag = false;
}

void UYUFSBelongingsRetrievalComponent::Observe(float DeltaTime, const FYUFSNPCObservation& Observation)
{
	Npc = Cast<AYUFSEvacuationNPC>(GetOwner());
	if (!Npc.IsValid() || !bEnabled || Npc->bUseExternalNavigationDriver) return;
	PrepareForEpisode();
	if (!bWantsBag) return;
	if (Phase != EYUFSBelongingsRetrievalPhase::LeftBehind || !Bag.IsValid()) return;

	const auto* State = Npc->GetBehaviorStateMachine();
	if (!State || !State->HasCommittedToEvacuation() || State->GetCurrentState() != EYUFSBehaviorState::Evacuating)
		return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (EvacuationStartedAt < 0.f) EvacuationStartedAt = Now;
	if (Now - EvacuationStartedAt < RememberDelay) return;

	// Remembered the bag: decide once whether to go back for it.
	FName Reason;
	if (!IsReturnSafe(Observation, Reason) || !HasSafeReturnPath(Reason))
	{
		Phase = EYUFSBelongingsRetrievalPhase::Abandoned;
		LastStopReason = Reason;
		PublishOpportunity();
		UE_LOG(LogTemp, Display, TEXT("[NPCBelongings] %s remembered the bag but keeps evacuating: %s"),
			*Npc->GetName(), *Reason.ToString());
		return;
	}
	BeginReturn();
}

void UYUFSBelongingsRetrievalComponent::BeginReturn()
{
	Phase = EYUFSBelongingsRetrievalPhase::Returning;
	PhaseStartedAt = GetWorld()->GetTimeSeconds();
	PickupElapsed = 0.f;
	HazardRecheckTimer = 0.5f;
	FailedPathSeconds = 0.f;
	MovementTarget = Bag->GetPickupLocation();
	LastStopReason = NAME_None;
	if (auto* Navigator = Npc->GetNavigator()) Navigator->ClearPath();
	PublishOpportunity();
	if (auto* Team = Npc->GetTeamIntegrationComponent())
		ExecutionRevision = Team->GetInteractionDirective().Revision;
	UE_LOG(LogTemp, Display, TEXT("[NPCBelongings] %s turns back for the bag at %s (%.0fcm away)"),
		*Npc->GetName(), *MovementTarget.ToCompactString(), FVector::Dist(Npc->GetActorLocation(), MovementTarget));
}

bool UYUFSBelongingsRetrievalComponent::Execute(float DeltaTime)
{
	if (!IsActive()) return false;
	if (!Npc.IsValid() || !bEnabled || !Bag.IsValid() || Bag->IsCarried())
	{ Finish(false, TEXT("BagUnavailable"), true); return false; }
	if (auto* Team = Npc->GetTeamIntegrationComponent())
	{
		const auto& Directive = Team->GetInteractionDirective();
		if (Directive.Goal == EYUFSInteractionGoal::RetrieveBelongings) ExecutionRevision = Directive.Revision;
	}

	FName Reason;
	if (!IsReturnSafe(Npc->GetLastObservation(), Reason)) { Finish(false, Reason, true); return false; }
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - PhaseStartedAt > MaxRetrievalSeconds) { Finish(false, TEXT("RetrievalTimeBudgetReached"), true); return false; }
	auto* Navigator = Npc->GetNavigator();
	const bool bPathFailed = Navigator && Navigator->GetNavigationStatus() == EYUFSNavigationStatus::Failed
		&& Navigator->GetRequestedDestination().Equals(MovementTarget, 100.f);
	FailedPathSeconds = bPathFailed ? FailedPathSeconds + FMath::Max(0.f, DeltaTime) : 0.f;
	// The navigator retries transient failures itself; give up only once it stops retrying.
	if (bPathFailed && (!Navigator->ShouldRetryPath() || FailedPathSeconds > 6.f))
	{ Finish(false, TEXT("ReturnPathFailed"), true); return false; }
	HazardRecheckTimer -= DeltaTime;
	if (HazardRecheckTimer <= 0.f && Navigator)
	{
		HazardRecheckTimer = 0.5f;
		// Smoke or heat seen on the way back is a reason to give up, not to push through.
		if (Navigator->IsFollowingPath() && IsObservedDangerAlong(Navigator->GetCurrentPathPoints()))
		{ Finish(false, TEXT("ReturnPathObservedDangerous"), true); return false; }
	}

	MovementTarget = Bag->GetPickupLocation();
	const FVector Here = Npc->GetActorLocation();
	const bool bAtBag = FVector::Dist2D(Here, MovementTarget) <= PickupRadiusCm
		&& FMath::Abs(Here.Z - MovementTarget.Z) < 160.f;
	auto* Animation = Npc->GetActionAnimationComponent();
	if (!bAtBag)
	{
		if (Phase == EYUFSBelongingsRetrievalPhase::PickingUp) Phase = EYUFSBelongingsRetrievalPhase::Returning;
		PickupElapsed = 0.f;
		// Walking binding while hurrying back; the NPC's own navigator owns locomotion.
		if (Animation && !Npc->bUseExternalMotionDriver)
			Animation->ApplyAction(EYUFSAction::HelpOther, EYUFSBehaviorState::Evacuating);
		return false;
	}

	Phase = EYUFSBelongingsRetrievalPhase::PickingUp;
	if (Navigator) Navigator->ClearPath();
	Npc->GetCharacterMovement()->StopMovementImmediately();
	const FRotator Facing(0.f, (MovementTarget - Here).Rotation().Yaw, 0.f);
	Npc->SetActorRotation(FMath::RInterpConstantTo(Npc->GetActorRotation(), Facing, DeltaTime, 240.f));
	if (Animation && !Npc->bUseExternalMotionDriver)
		Animation->ApplyAction(EYUFSAction::GatherBelongings, EYUFSBehaviorState::Normal);
	PickupElapsed += FMath::Max(0.f, DeltaTime);
	if (PickupElapsed < PickupSeconds) return true;

	if (!Bag->AttachToCarrier(Npc.Get())) { Finish(false, TEXT("PickupFailed"), true); return false; }
	Finish(true, TEXT("BelongingsCollected"), true);
	return true;
}

void UYUFSBelongingsRetrievalComponent::Finish(bool bSuccess, FName Reason, bool bResumeEvacuation)
{
	const bool bWasActive = IsActive();
	LastStopReason = Reason;
	Phase = bSuccess ? EYUFSBelongingsRetrievalPhase::Carrying : EYUFSBelongingsRetrievalPhase::Abandoned;
	PickupElapsed = 0.f;
	MovementTarget = FVector::ZeroVector;
	if (!Npc.IsValid()) return;
	PublishOpportunity();
	if (!bWasActive) return;
	if (auto* Navigator = Npc->GetNavigator()) Navigator->ClearPath(); // Drops late async results toward the bag.
	if (auto* Team = Npc->GetTeamIntegrationComponent())
	{
		FYUFSTeamRequestFeedback Feedback;
		Feedback.RequestRevision = ExecutionRevision;
		Feedback.Status = bSuccess ? EYUFSTeamRequestStatus::Completed : EYUFSTeamRequestStatus::Cancelled;
		Feedback.Reason = Reason;
		Feedback.ResolvedLocation = Npc->GetActorLocation();
		Team->SubmitInteractionFeedback(Feedback);
	}
	if (auto* Selector = Npc->GetHumanBehaviorSelector()) Selector->RequestReselection(Reason);
	UE_LOG(LogTemp, Display, TEXT("[NPCBelongings] %s %s (%s); evacuating again"),
		*Npc->GetName(), bSuccess ? TEXT("picked up the bag") : TEXT("gave up on the bag"), *Reason.ToString());
	if (bResumeEvacuation) Npc->ResumeEvacuationAfterInteraction();
}

void UYUFSBelongingsRetrievalComponent::Cancel(bool bResumeEvacuation)
{
	if (IsActive()) Finish(false, TEXT("RetrievalInterrupted"), bResumeEvacuation);
}

void UYUFSBelongingsRetrievalComponent::DestroyBag()
{
	if (Bag.IsValid())
	{
		// Level-placed bags belong to the map: never delete them. One that was carried out
		// leaves with its owner; one that was left behind stays (or goes back) on its spot.
		if (Bag->IsLevelPlaced()) Bag->IsCarried() ? Bag->LeaveWithCarrier() : Bag->ReleaseToHome();
		else Bag->Destroy();
	}
	Bag.Reset();
}

void UYUFSBelongingsRetrievalComponent::ResetForEpisode()
{
	Cancel(false);
	DestroyBag();
	Phase = EYUFSBelongingsRetrievalPhase::None;
	MovementTarget = FVector::ZeroVector;
	LastStopReason = NAME_None;
	EvacuationStartedAt = -1.f;
	PhaseStartedAt = PickupElapsed = 0.f;
	ExecutionRevision = 0;
	bRolled = false;
	bWantsBag = false;
	++EpisodeCounter;
	if (Npc.IsValid()) PublishOpportunity();
}

FYUFSBehaviorDecision UYUFSBelongingsRetrievalComponent::MakeDecision() const
{
	FYUFSBehaviorDecision Decision;
	Decision.Behavior = EYUFSHighLevelBehavior::RetrieveBelongings;
	Decision.LegacyAction = EYUFSAction::GatherBelongings;
	Decision.DesiredTask = EYUFSActionTask::GatherBelongings;
	Decision.Reason = TEXT("ReturnForLeftBelongings");
	return Decision;
}
