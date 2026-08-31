#include "NPC/YUFSEvacuationNPC.h"

#include "AIController.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "NPC/Behavior/YUFSBehaviorConfig.h"
#include "Communication/YUFSCommTypes.h"
#include "Communication/YUFSEmergencyCommSystem.h"
#include "Animation/AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/YUFSExperienceLogger.h"
#include "Core/YUFSObservation.h"
#include "Debug/YUFSNPCDebugComponent.h"
#include "EngineUtils.h"
#include "Fire/YUFSBinaryManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Level/YUFSLevelDataManager.h"
#include "Navigation/YUFSSmokeAwareNavigator.h"
#include "NavigationSystem.h"
#include "NPC/Decision/YUFSBehaviorDecisionModel.h"
#include "Perception/YUFSNPCPerceptionComponent.h"
#include "Simulation/YUFSSimulationController.h"
#include "Social/YUFSSocialInfluenceComponent.h"
#include "Misc/Crc.h"

AYUFSEvacuationNPC::AYUFSEvacuationNPC()
{
	PrimaryActorTick.bCanEverTick = true;

	AIControllerClass = AAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	PerceptionComp = CreateDefaultSubobject<UYUFSNPCPerceptionComponent>(TEXT("YUFSNPCPerceptionComponent"));
	BehaviorSM     = CreateDefaultSubobject<UYUFSBehaviorStateMachine>(TEXT("YUFSBehaviorStateMachine"));
	Navigator      = CreateDefaultSubobject<UYUFSSmokeAwareNavigator>(TEXT("YUFSSmokeAwareNavigator"));
	SocialComp     = CreateDefaultSubobject<UYUFSSocialInfluenceComponent>(TEXT("YUFSSocialInfluenceComponent"));
	DebugComp      = CreateDefaultSubobject<UYUFSNPCDebugComponent>(TEXT("YUFSNPCDebugComponent"));

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw   = false;
	bUseControllerRotationRoll  = false;

	if (GetCharacterMovement())
	{
		GetCharacterMovement()->bOrientRotationToMovement = true;
		GetCharacterMovement()->RotationRate = FRotator(0.f, 500.f, 0.f);
		GetCharacterMovement()->bUseRVOAvoidance = true;
		GetCharacterMovement()->AvoidanceConsiderationRadius = 300.f;
		GetCharacterMovement()->AvoidanceWeight = 0.75f;
	}

	if (UCapsuleComponent* Cap = GetCapsuleComponent())
		Cap->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);

	if (USkeletalMeshComponent* MeshComp = GetMesh())
		MeshComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
}

void AYUFSEvacuationNPC::BeginPlay()
{
	Super::BeginPlay();
	SpawnLocation = GetActorLocation();
	LastMovementSampleLocation = SpawnLocation;
	LastPositionCheckLocation  = SpawnLocation;
	bHasMovementSample = true;
	InitializeBehaviorRandomStreams();

	if (BehaviorSM)
	{
		BehaviorSM->SetExternalCommitControl(bUseCalibratedBehaviorModel);
	}

	if (AYUFSEmergencyCommSystem* CommSystem = Cast<AYUFSEmergencyCommSystem>(
		UGameplayStatics::GetActorOfClass(GetWorld(), AYUFSEmergencyCommSystem::StaticClass())))
	{
		CommSystem->OnEmergencyComm.AddDynamic(this, &AYUFSEvacuationNPC::OnCommReceived);
	}

	for (TActorIterator<AYUFSBinaryManager> It(GetWorld()); It; ++It)  { BinaryManager = *It; break; }
	for (TActorIterator<AYUFSLevelDataManager> It(GetWorld()); It; ++It){ LevelDataMgr  = *It; break; }

	for (TActorIterator<AYUFSSimulationController> It(GetWorld()); It; ++It)
	{
		SimulationController = *It;
		SimulationController->RegisterNPC(this);
		break;
	}
}

void AYUFSEvacuationNPC::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// ── 타임라인 관찰 모드 ─────────────────────────────────────────────
	// 관찰 모드에서는 AI 판단, 경로 탐색, 이동 입력을 다시 계산하면 안 됩니다.
	// 저장된 스냅샷만 SimulationController/TimelineRecorder가 적용합니다.
	if (bTimelinePlaybackMode)
	{
		if (Navigator) Navigator->ClearPath();
		if (UCharacterMovementComponent* Mv = GetCharacterMovement())
		{
			Mv->StopMovementImmediately();
			Mv->DisableMovement();
		}
		return;
	}

	// ── 시뮬레이션 일시정지 ───────────────────────────────────────────────
	if (SimulationController && !SimulationController->IsNPCSimulationEnabled())
	{
		if (Navigator) Navigator->ClearPath();
		if (UCharacterMovementComponent* Mv = GetCharacterMovement())
		{
			Mv->StopMovementImmediately();
			Mv->MaxWalkSpeed = 0.f;
		}
		return;
	}

	const int32 CurrentFrame = GetCurrentSimFrame();

	// ── 지각 / 사회 갱신 (SM 및 MLP 정책이 최신 데이터 사용) ───────────
	if (PerceptionComp) PerceptionComp->UpdatePerception(CurrentFrame);
	if (SocialComp)     SocialComp->UpdateSocialContext();

	// ── PADM 상태머신 갱신 ───────────────────────────────────────────────
	if (BehaviorSM)
	{
		FYUFSNPCObservation Obs{};
		BuildObservation(Obs);
		BehaviorSM->TickStateMachine(DeltaTime, Obs);
	}

	// ── MLP 정책 추론 및 액션 실행 ───────────────────────────────────────
	TickPolicy(DeltaTime);

	// ── 이동 속도 제한 (Crawl / Incapacitated) ───────────────────────────
	if (UCharacterMovementComponent* Mv = GetCharacterMovement())
	{
		if (BehaviorSM && BehaviorSM->Config)
		{
			if (BehaviorSM->IsIncapacitated())
			{
				Mv->MaxWalkSpeed = 0.f;
				if (Navigator) Navigator->ClearPath();
			}
			else if (BehaviorSM->IsCrawling())
			{
				Mv->MaxWalkSpeed = FMath::Min(Mv->MaxWalkSpeed, BehaviorSM->Config->CrawlSpeed);
			}
		}
	}

	// ── 스턱 감지 ─────────────────────────────────────────────────────────
	UpdateStuckDetection(DeltaTime);

	// ── CSV 로깅 ──────────────────────────────────────────────────────────
	FYUFSNPCObservation CurrentObs{};
	BuildObservation(CurrentObs);
	const EYUFSTerminalReason TerminalReason = GetCurrentTerminalReason();
	FlushLearningTransition(CurrentObs, TerminalReason);

	if (TerminalReason != EYUFSTerminalReason::None)
	{
		if (UCharacterMovementComponent* Mv = GetCharacterMovement()) Mv->MaxWalkSpeed = 0.f;
		if (Navigator) Navigator->ClearPath();
	}

	PrevObservation = CurrentObs;
	bHasPendingTransition = bLogTransitions;
}

void AYUFSEvacuationNPC::DriveMovementToward(FVector Target)
{
	if (!Navigator || Target.IsZero()) return;

	// 일시정지 → 재개 시 MaxWalkSpeed 가 0 으로 남아있는 경우 복원
	// (Crawling 속도 제한은 NPC Tick 에서 별도로 cap 하므로 여기선 무조건 양수만 보장)
	if (UCharacterMovementComponent* Mv = GetCharacterMovement())
	{
		if (Mv->MaxWalkSpeed < 1.f)
			Mv->MaxWalkSpeed = 300.f;
	}

	Navigator->UpdateWaypoint(GetActorLocation(), 80.f);
	const FVector SteeringTarget = Navigator->GetSteeringTarget(GetActorLocation(), 120.f);

	FVector Dir = SteeringTarget - GetActorLocation();
	Dir.Z = 0.f;

	if (!Dir.IsNearlyZero(1.f))
	{
		Dir.Normalize();
		const float SpeedMult = SocialComp ? SocialComp->GetGroupSpeedMultiplier() : 1.f;
		AddMovementInput(Dir, SpeedMult);
	}
}

void AYUFSEvacuationNPC::SetMovementSpeed(float Speed)
{
	if (UCharacterMovementComponent* Mv = GetCharacterMovement())
		Mv->MaxWalkSpeed = Speed;
}

void AYUFSEvacuationNPC::UpdateStuckDetection(float DeltaTime)
{
	UCharacterMovementComponent* Mv = GetCharacterMovement();
	if (!Mv || !Navigator) return;

	const FVector Pos = GetActorLocation();
	const float MaxSpeed = Mv->MaxWalkSpeed;
	if (MaxSpeed < KINDA_SMALL_NUMBER) return;

	// 경로가 없으면 스턱 감지 불필요
	if (Navigator->GetCurrentDestination().IsZero()) return;

	// 이동속도 기반 스턱
	const float Moved = bHasMovementSample ? FVector::Dist2D(Pos, LastMovementSampleLocation) : 0.f;
	const float Expected = MaxSpeed * DeltaTime;
	const bool bSlow = Expected > KINDA_SMALL_NUMBER && Moved < Expected * 0.1f;
	const bool bHasVel = GetVelocity().Size2D() > MaxSpeed * 0.3f;

	if (bSlow && bHasVel)
	{
		StuckTimer += DeltaTime;
		if (StuckTimer > 1.f)
		{
			const FVector Dest = Navigator->GetCurrentDestination();
			Navigator->ClearPath();
			if (!Dest.IsZero()) Navigator->RequestPathAsync(Dest, GetCurrentSimFrame());
			StuckTimer = 0.f;
		}
	}
	else StuckTimer = 0.f;

	// 지오메트리 침투 스턱
	const float PosDelta = FVector::Dist2D(Pos, LastPositionCheckLocation);
	if (GetVelocity().Size2D() > 20.f && PosDelta < 5.f && bHasMovementSample)
	{
		PositionStuckTimer += DeltaTime;
		if (PositionStuckTimer > 2.5f)
		{
			if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
			{
				FNavLocation NavLoc;
				if (NavSys->GetRandomReachablePointInRadius(Pos, 300.f, NavLoc))
				{
					TeleportTo(NavLoc.Location + FVector(0.f, 0.f, 10.f), GetActorRotation());
					const FVector Dest = Navigator->GetCurrentDestination();
					Navigator->ClearPath();
					if (!Dest.IsZero()) Navigator->RequestPathAsync(Dest, GetCurrentSimFrame());
				}
			}
			PositionStuckTimer = 0.f;
		}
	}
	else PositionStuckTimer = 0.f;

	if (PosDelta >= 5.f || !bHasMovementSample) LastPositionCheckLocation = Pos;
	LastMovementSampleLocation = Pos;
	bHasMovementSample = true;
}

void AYUFSEvacuationNPC::OnCommReceived(EYUFSCommType CommType, FVector SourceLocation, float EffectiveRadius, FVector GuidanceTarget)
{
	if (FVector::DistSquared(GetActorLocation(), SourceLocation) > EffectiveRadius * EffectiveRadius) return;

	switch (CommType)
	{
	case EYUFSCommType::AlarmOnly:
		bAlarmSounding = true;
		if (BehaviorSM) BehaviorSM->OnAlarmReceived();
		break;
	case EYUFSCommType::PreRecordedMessage:
		bReceivedPreRecordedMsg = true;
		if (BehaviorSM) BehaviorSM->OnPreRecordedMessageReceived();
		break;
	case EYUFSCommType::LiveAnnouncement:
		bReceivedLiveAnnouncement = true;
		if (BehaviorSM) BehaviorSM->OnLiveAnnouncementReceived();
		break;
	case EYUFSCommType::StaffGuidance:
		bReceivedStaffGuidance = true;
		StaffGuidedExitLocation = GuidanceTarget;
		if (BehaviorSM) BehaviorSM->OnStaffGuidanceReceived();
		break;
	}
}

int32 AYUFSEvacuationNPC::GetCurrentSimFrame() const
{
	return BinaryManager ? BinaryManager->GetCurrentFrame() : 0;
}

void AYUFSEvacuationNPC::BuildObservation(FYUFSNPCObservation& Out) const
{
	Out = FYUFSNPCObservation{};
	if (!PerceptionComp || !BehaviorSM || !SocialComp || !Navigator || !LevelDataMgr) return;

	Out.SmokeDensityAtSelf      = PerceptionComp->GetSmokeDensity();
	Out.TemperatureAtSelf       = PerceptionComp->GetTemperature();
	Out.SmokeInFrontNormalized  = PerceptionComp->GetSmokeInFrontNormalized();
	Out.SmokeAboveNormalized    = PerceptionComp->GetSmokeAboveNormalized();
	Out.RiskLevel               = PerceptionComp->GetRiskLevel();
	Out.CurrentState            = BehaviorSM->GetCurrentState();
	Out.RiskPerception          = BehaviorSM->GetRiskPerception();
	Out.StressLevel             = PerceptionComp->GetRiskLevel();
	Out.SmokeExposureAccumulated= BehaviorSM->GetSmokeExposure();
	Out.MillingActionCount      = MillingActionCount;
	Out.StaffGuidedExitLocation = StaffGuidedExitLocation;
	Out.bAlarmSounding          = bAlarmSounding;
	Out.bReceivedPreRecordedMsg = bReceivedPreRecordedMsg;
	Out.bReceivedLiveAnnouncement = bReceivedLiveAnnouncement;
	Out.bReceivedStaffGuidance  = bReceivedStaffGuidance;
	Out.NearbyEvacuatingRatio   = SocialComp->GetNearbyEvacuatingRatio();
	Out.NearbyNPCCount          = SocialComp->GetNearbyNPCCount();
	Out.GroupSize               = SocialComp->GetNearbyNPCCount() + 1;
	Out.bNearbyNPCNeedsHelp     = SocialComp->ShouldHelpNearbyNPC();

	const FVector Pos   = GetActorLocation();
	const int32 Frame   = GetCurrentSimFrame();
	const FVector NExit = LevelDataMgr->GetNearestSafeExit(Pos, true, Frame);
	const FVector FExit = LevelDataMgr->GetFamiliarExit(SpawnLocation);

	Out.DistToNearestExit    = FVector::Dist(Pos, NExit);
	Out.DistToFamiliarExit   = FVector::Dist(Pos, FExit);
	Out.DirToNearestExit     = (NExit - Pos).GetSafeNormal();
	Out.SimTimeNormalized    = FMath::Clamp(static_cast<float>(Frame) / 8000.f, 0.f, 1.f);
	Out.bNearestExitSmokeFree= !LevelDataMgr->IsLocationDangerous(NExit, Frame);
}

EYUFSTerminalReason AYUFSEvacuationNPC::GetCurrentTerminalReason() const
{
	if (BehaviorSM && BehaviorSM->IsIncapacitated())
		return EYUFSTerminalReason::Incapacitated;

	if (LevelDataMgr)
	{
		const FVector Exit = LevelDataMgr->GetNearestSafeExit(GetActorLocation(), false, GetCurrentSimFrame());
		if (FVector::Dist(GetActorLocation(), Exit) < 150.f)
			return EYUFSTerminalReason::ReachedExit;
	}

	return EYUFSTerminalReason::None;
}


FYUFSTimelineNPCSnapshot AYUFSEvacuationNPC::BuildTimelineSnapshot() const
{
	FYUFSTimelineNPCSnapshot Snapshot;
	Snapshot.NPCId = GetFName();
	Snapshot.Location = GetActorLocation();
	Snapshot.Rotation = GetActorRotation();
	Snapshot.CurrentAction = CurrentAction;
	Snapshot.bVisible = !IsHidden();
	Snapshot.bEvacuated = false;
	Snapshot.bIncapacitated = false;

	if (BehaviorSM)
	{
		Snapshot.BehaviorState = BehaviorSM->GetCurrentState();
		Snapshot.RiskPerception = BehaviorSM->GetRiskPerception();
		Snapshot.SmokeExposure = BehaviorSM->GetSmokeExposure();
		Snapshot.bIncapacitated = BehaviorSM->IsIncapacitated();
	}

	// SimulationController가 대피 완료 NPC를 숨겼다면 관찰 모드에서도 숨김 상태로 기록합니다.
	// 별도 bEvacuated 플래그는 Controller가 필요 시 확장할 수 있도록 남겨둡니다.
	if (IsHidden() && !Snapshot.bIncapacitated)
	{
		Snapshot.bEvacuated = true;
	}

	return Snapshot;
}

void AYUFSEvacuationNPC::ApplyTimelineSnapshot(const FYUFSTimelineNPCSnapshot& Snapshot)
{
	// 관찰 모드에서는 물리 이동이 아니라 기록된 위치로 직접 배치합니다.
	SetActorLocationAndRotation(Snapshot.Location, Snapshot.Rotation, false, nullptr, ETeleportType::TeleportPhysics);

	SetActorHiddenInGame(!Snapshot.bVisible);
	SetActorEnableCollision(Snapshot.bVisible);

	CurrentAction = Snapshot.CurrentAction;

	if (Navigator)
	{
		Navigator->ClearPath();
	}

	if (UCharacterMovementComponent* Mv = GetCharacterMovement())
	{
		Mv->StopMovementImmediately();
		Mv->DisableMovement();
	}

	// 현재 BehaviorStateMachine은 외부에서 상태를 강제로 세팅하는 API가 없으므로
	// 상태값은 Snapshot에 저장하되, 실제 AI 상태머신은 관찰 모드에서 갱신하지 않습니다.
}

void AYUFSEvacuationNPC::SetTimelinePlaybackMode(bool bEnabled)
{
	if (bTimelinePlaybackMode == bEnabled)
	{
		return;
	}

	bTimelinePlaybackMode = bEnabled;

	if (UCharacterMovementComponent* Mv = GetCharacterMovement())
	{
		if (bEnabled)
		{
			SavedWalkSpeedBeforeTimeline = Mv->MaxWalkSpeed;
			Mv->StopMovementImmediately();
			Mv->DisableMovement();
		}
		else
		{
			Mv->SetMovementMode(MOVE_Walking);
			Mv->MaxWalkSpeed = FMath::Max(1.f, SavedWalkSpeedBeforeTimeline);
		}
	}

	if (Navigator)
	{
		Navigator->ClearPath();
	}

	SetActorTickEnabled(true);
}

void AYUFSEvacuationNPC::NotifyEpisodeFinished(EYUFSTerminalReason TerminalReason)
{
	if (!bHasPendingTransition) return;

	FYUFSNPCObservation TerminalObs{};
	BuildObservation(TerminalObs);
	FlushLearningTransition(TerminalObs, TerminalReason);
}

void AYUFSEvacuationNPC::FlushLearningTransition(const FYUFSNPCObservation& NextObs, EYUFSTerminalReason TerminalReason)
{
	if (!bHasPendingTransition || !bLogTransitions) return;

	const bool bDone = TerminalReason != EYUFSTerminalReason::None;

	FYUFSExperienceLogger::LogTransition(
		GetName(),
		SimulationController ? SimulationController->GetCurrentRunIndex() : 0,
		TransitionStepIndex,
		GetCurrentSimFrame(),
		SimulationController ? SimulationController->GetElapsedTime() : 0.f,
		PrevObservation,
		CurrentAction,
		NextObs,
		bDone,
		TerminalReason);

	++TransitionStepIndex;
	bHasPendingTransition = !bDone;
}

// ── MLP 정책 실행 메서드 ────────────────────────────────────────────────────

void AYUFSEvacuationNPC::TickPolicy(float DeltaTime)
{
	if (!BehaviorSM) return;
	if (BehaviorSM->IsIncapacitated()) return;

	ActionHoldTimer         += DeltaTime;
	PolicyTickAccumulator   += DeltaTime;

	if (PolicyTickAccumulator >= PolicyTickInterval)
	{
		PolicyTickAccumulator = 0.f;

		FYUFSNPCObservation Obs{};
		BuildObservation(Obs);

		if (bUseCalibratedBehaviorModel && BehaviorSM->Config)
		{
			TickCalibratedPolicy(Obs);
		}
		else
		{
			MLPolicy.SetDataCollectionMode(bDataCollectionMode);
			const EYUFSAction NewAction = MLPolicy.SelectAction(Obs);

			if (Obs.CurrentState == EYUFSBehaviorState::Milling)
				++MillingActionCount;

			const bool bStateChanged = Obs.CurrentState != LastPolicyBehaviorState;
			const bool bHeldLongEnough = ActionHoldTimer >= MinActionHoldDuration;

			if (NewAction != CurrentAction && (bStateChanged || bHeldLongEnough))
			{
				ActionHoldTimer = 0.f;
				OnActionChanged(NewAction);
				CurrentAction = NewAction;
			}

			LastPolicyBehaviorState = Obs.CurrentState;
		}
	}

	ExecuteCurrentAction(DeltaTime);
}

void AYUFSEvacuationNPC::InitializeBehaviorRandomStreams()
{
	const uint32 AgentHash = FCrc::StrCrc32(*GetName());
	ResolvedBehaviorSeed = BehaviorRandomSeed ^ static_cast<int32>(AgentHash);
	DecisionRandomStream.Initialize(ResolvedBehaviorSeed ^ 0x13579BDF);
	DurationRandomStream.Initialize(ResolvedBehaviorSeed ^ 0x02468ACE);
	RouteRandomStream.Initialize(ResolvedBehaviorSeed ^ 0x10203040);

	UE_LOG(LogTemp, Log, TEXT("[YUFS][Decision] agent=%s seed=%d"), *GetName(), ResolvedBehaviorSeed);
}

void AYUFSEvacuationNPC::TickCalibratedPolicy(const FYUFSNPCObservation& Obs)
{
	const EYUFSBehaviorState State = Obs.CurrentState;
	const bool bStateChanged = State != LastPolicyBehaviorState;

	if (IsPreEvacuationState(State)
		&& (Obs.bReceivedStaffGuidance || Obs.bReceivedLiveAnnouncement))
	{
		UE_LOG(LogTemp, Log, TEXT("[YUFS][Decision] agent=%s override=OfficialInstruction"), *GetName());
		BehaviorSM->CommitToEvacuation();
		bRouteDecisionMade = false;
		SelectRouteDecision(Obs);
		LastPolicyBehaviorState = BehaviorSM->GetCurrentState();
		return;
	}

	if (State == EYUFSBehaviorState::Normal)
	{
		if (bStateChanged || CurrentAction != EYUFSAction::Idle)
		{
			OnActionChanged(EYUFSAction::Idle);
			CurrentAction = EYUFSAction::Idle;
			ActionHoldTimer = 0.f;
		}
	}
	else if (IsPreEvacuationState(State))
	{
		if (!bIntentDecisionMade)
		{
			MakeInitialIntentDecision(Obs);
		}
		else if (ActionHoldTimer >= PlannedActionDuration)
		{
			++CompletedPreEvacuationActionCount;
			MillingActionCount = CompletedPreEvacuationActionCount;

			if (CompletedPreEvacuationActionCount >= TargetPreEvacuationActionCount)
			{
				BehaviorSM->CommitToEvacuation();
				bRouteDecisionMade = false;
				SelectRouteDecision(Obs);
			}
			else
			{
				SelectNextPreEvacuationAction();
			}
		}
	}
	else if (State == EYUFSBehaviorState::Evacuating)
	{
		const bool bNewGuidance = Obs.bReceivedStaffGuidance
			&& SelectedRouteStrategy != EYUFSRouteStrategy::CrowdOrLeader;
		if (!bRouteDecisionMade || IsCurrentRouteUnsafe() || bNewGuidance)
		{
			SelectRouteDecision(Obs);
		}
		else if (bStateChanged)
		{
			ApplySelectedRouteAction();
		}
	}
	else if (State == EYUFSBehaviorState::Helping)
	{
		if (bStateChanged || CurrentAction != EYUFSAction::HelpOther)
		{
			OnActionChanged(EYUFSAction::HelpOther);
			CurrentAction = EYUFSAction::HelpOther;
			ActionHoldTimer = 0.f;
		}
	}
	else if (State == EYUFSBehaviorState::Crawling)
	{
		FVector SafeExit = FVector::ZeroVector;
		SelectedRouteStrategy = LevelDataMgr
			&& LevelDataMgr->TryGetNearestSafeExit(GetActorLocation(), GetCurrentSimFrame(), SafeExit)
			? EYUFSRouteStrategy::NearestSafeExit
			: EYUFSRouteStrategy::ShelterInPlace;
		bRouteDecisionMade = true;
		const EYUFSAction ExpectedAction = SelectedRouteStrategy == EYUFSRouteStrategy::NearestSafeExit
			? EYUFSAction::EvacuateToNearestExit
			: EYUFSAction::WaitForInfo;
		if (bStateChanged || CurrentAction != ExpectedAction)
		{
			ApplySelectedRouteAction();
		}
	}

	LastPolicyBehaviorState = BehaviorSM->GetCurrentState();
}

void AYUFSEvacuationNPC::MakeInitialIntentDecision(const FYUFSNPCObservation& Obs)
{
	const UYUFSBehaviorConfig* Config = BehaviorSM ? BehaviorSM->Config : nullptr;
	if (!Config) return;

	ActiveDangerCue = DetermineDangerCue(Obs);
	TArray<float> LikelihoodRatios;
	if (bSafetyTrained) LikelihoodRatios.Add(Config->SafetyTrainingLikelihoodRatio);
	if (Obs.bReceivedPreRecordedMsg) LikelihoodRatios.Add(Config->OfficialInformationLikelihoodRatio);
	if (Obs.NearbyEvacuatingRatio >= Config->MovingCrowdRatioThreshold)
	{
		LikelihoodRatios.Add(Config->MovingCrowdLikelihoodRatio);
	}

	const float CommitProbability = FYUFSBehaviorDecisionModel::ComputeCommitProbability(
		GetBaseCommitProbability(ActiveDangerCue),
		LikelihoodRatios);
	const float Roll = DecisionRandomStream.GetFraction();
	bIntentDecisionMade = true;

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[YUFS][Decision] agent=%s cue=%s p_commit=%.4f roll=%.4f seed=%d"),
		*GetName(),
		*StaticEnum<EYUFSDangerCue>()->GetNameStringByValue(static_cast<int64>(ActiveDangerCue)),
		CommitProbability,
		Roll,
		ResolvedBehaviorSeed);

	if (Roll < CommitProbability)
	{
		BehaviorSM->CommitToEvacuation();
		bRouteDecisionMade = false;
		SelectRouteDecision(Obs);
		return;
	}

	TargetPreEvacuationActionCount = FYUFSBehaviorDecisionModel::SelectPreEvacuationActionCount(
		DecisionRandomStream,
		Config->ShortActionCountWeight,
		Config->MediumActionCountWeight,
		Config->LongActionCountWeight);
	CompletedPreEvacuationActionCount = 0;
	SelectNextPreEvacuationAction();
}

void AYUFSEvacuationNPC::SelectNextPreEvacuationAction()
{
	const UYUFSBehaviorConfig* Config = BehaviorSM ? BehaviorSM->Config : nullptr;
	if (!Config) return;

	const TArray<FYUFSActionWeight> Candidates = {
		FYUFSActionWeight(EYUFSAction::SeekInformation, Config->SeekInformationWeight),
		FYUFSActionWeight(EYUFSAction::WaitForInfo, Config->WaitAndObserveWeight),
		FYUFSActionWeight(EYUFSAction::GatherBelongings, Config->GatherBelongingsWeight),
		FYUFSActionWeight(EYUFSAction::AlertNearbyOccupants, Config->AlertAndHelpWeight),
		FYUFSActionWeight(EYUFSAction::AttemptInitialFirefighting, Config->InitialFirefightingWeight)
	};

	const EYUFSAction NewAction = FYUFSBehaviorDecisionModel::SelectWeightedAction(DecisionRandomStream, Candidates);
	PlannedActionDuration = FYUFSBehaviorDecisionModel::SelectDuration(
		DurationRandomStream,
		GetActionDurationRange(NewAction));
	ActionHoldTimer = 0.f;
	OnActionChanged(NewAction);
	CurrentAction = NewAction;

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[YUFS][Decision] agent=%s task=%s task_index=%d/%d duration=%.2f"),
		*GetName(),
		*StaticEnum<EYUFSAction>()->GetNameStringByValue(static_cast<int64>(NewAction)),
		CompletedPreEvacuationActionCount + 1,
		TargetPreEvacuationActionCount,
		PlannedActionDuration);
}

void AYUFSEvacuationNPC::SelectRouteDecision(const FYUFSNPCObservation& Obs)
{
	const UYUFSBehaviorConfig* Config = BehaviorSM ? BehaviorSM->Config : nullptr;
	if (!Config || !LevelDataMgr) return;

	const FVector Position = GetActorLocation();
	const int32 Frame = GetCurrentSimFrame();

	if (Obs.bReceivedStaffGuidance
		&& !StaffGuidedExitLocation.IsZero()
		&& !LevelDataMgr->IsLocationDangerous(StaffGuidedExitLocation, Frame))
	{
		SelectedRouteStrategy = EYUFSRouteStrategy::CrowdOrLeader;
		bRouteDecisionMade = true;
		ApplySelectedRouteAction();
		UE_LOG(LogTemp, Log, TEXT("[YUFS][Decision] agent=%s route=CrowdOrLeader source=StaffGuidance frame=%d"), *GetName(), Frame);
		return;
	}

	FVector NearestSafeExit = FVector::ZeroVector;
	const bool bHasNearestSafeExit = LevelDataMgr->TryGetNearestSafeExit(Position, Frame, NearestSafeExit);
	const FVector FamiliarExit = LevelDataMgr->GetFamiliarExit(SpawnLocation);
	const bool bHasSafeFamiliarExit = FVector::DistSquared(Position, FamiliarExit) > FMath::Square(100.f)
		&& !LevelDataMgr->IsLocationDangerous(FamiliarExit, Frame);
	const FVector CrowdDestination = SocialComp ? SocialComp->GetAverageEvacuationDestination() : FVector::ZeroVector;
	const bool bHasSafeCrowdDestination = !CrowdDestination.IsZero()
		&& !LevelDataMgr->IsLocationDangerous(CrowdDestination, Frame);

	const auto DistanceUtility = [Position, Config](FVector Destination)
	{
		return -Config->RouteDistanceUtilityScale
			* FMath::Clamp(FVector::Dist(Position, Destination) / 10000.f, 0.f, 10.f);
	};

	const TArray<FYUFSRouteCandidate> Candidates = {
		FYUFSRouteCandidate(
			EYUFSRouteStrategy::FamiliarExit,
			Config->FamiliarRoutePriorWeight,
			bHasSafeFamiliarExit ? DistanceUtility(FamiliarExit) : 0.f,
			bHasSafeFamiliarExit),
		FYUFSRouteCandidate(
			EYUFSRouteStrategy::CrowdOrLeader,
			Config->CrowdOrLeaderRoutePriorWeight,
			bHasSafeCrowdDestination
				? DistanceUtility(CrowdDestination) + Config->CrowdEvidenceUtilityScale * Obs.NearbyEvacuatingRatio
				: 0.f,
			bHasSafeCrowdDestination),
		FYUFSRouteCandidate(
			EYUFSRouteStrategy::NearestSafeExit,
			Config->NearestSafeRoutePriorWeight,
			bHasNearestSafeExit ? DistanceUtility(NearestSafeExit) : 0.f,
			bHasNearestSafeExit)
	};

	SelectedRouteStrategy = FYUFSBehaviorDecisionModel::SelectRoute(RouteRandomStream, Candidates);
	bRouteDecisionMade = true;
	ApplySelectedRouteAction();

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[YUFS][Decision] agent=%s route=%s frame=%d"),
		*GetName(),
		*StaticEnum<EYUFSRouteStrategy>()->GetNameStringByValue(static_cast<int64>(SelectedRouteStrategy)),
		Frame);
}

void AYUFSEvacuationNPC::ApplySelectedRouteAction()
{
	EYUFSAction NewAction = EYUFSAction::WaitForInfo;
	switch (SelectedRouteStrategy)
	{
	case EYUFSRouteStrategy::FamiliarExit:
		NewAction = EYUFSAction::EvacuateToFamiliarExit;
		break;
	case EYUFSRouteStrategy::CrowdOrLeader:
		NewAction = bReceivedStaffGuidance
			? EYUFSAction::EvacuateToNearestExit
			: EYUFSAction::FollowCrowd;
		break;
	case EYUFSRouteStrategy::NearestSafeExit:
		NewAction = EYUFSAction::EvacuateToNearestExit;
		break;
	case EYUFSRouteStrategy::ShelterInPlace:
	default:
		NewAction = EYUFSAction::WaitForInfo;
		break;
	}

	ActionHoldTimer = 0.f;
	OnActionChanged(NewAction);
	CurrentAction = NewAction;
}

EYUFSDangerCue AYUFSEvacuationNPC::DetermineDangerCue(const FYUFSNPCObservation& Obs) const
{
	const UYUFSBehaviorConfig* Config = BehaviorSM ? BehaviorSM->Config : nullptr;
	if (!Config) return EYUFSDangerCue::None;

	if (Obs.TemperatureAtSelf >= Config->FlameOrHighHeatTemperatureThreshold)
		return EYUFSDangerCue::FlameOrHighHeat;

	if (Obs.SmokeDensityAtSelf >= Config->SmokeAwarenessThreshold
		|| Obs.SmokeInFrontNormalized >= Config->VisionSmokeCueThreshold
		|| Obs.SmokeAboveNormalized >= Config->AboveSmokeCueThreshold)
	{
		return EYUFSDangerCue::Smoke;
	}

	if (Obs.bAlarmSounding || Obs.bReceivedPreRecordedMsg
		|| Obs.bReceivedLiveAnnouncement || Obs.bReceivedStaffGuidance)
	{
		return EYUFSDangerCue::AlarmOnly;
	}

	return EYUFSDangerCue::None;
}

float AYUFSEvacuationNPC::GetBaseCommitProbability(EYUFSDangerCue Cue) const
{
	const UYUFSBehaviorConfig* Config = BehaviorSM ? BehaviorSM->Config : nullptr;
	if (!Config) return 0.f;

	switch (Cue)
	{
	case EYUFSDangerCue::AlarmOnly: return Config->AlarmOnlyImmediateEvacuationProbability;
	case EYUFSDangerCue::Smoke: return Config->SmokeImmediateEvacuationProbability;
	case EYUFSDangerCue::FlameOrHighHeat: return Config->FlameOrHeatImmediateEvacuationProbability;
	default: return 0.f;
	}
}

FVector2D AYUFSEvacuationNPC::GetActionDurationRange(EYUFSAction Action) const
{
	const UYUFSBehaviorConfig* Config = BehaviorSM ? BehaviorSM->Config : nullptr;
	if (!Config) return FVector2D(MinActionHoldDuration, MinActionHoldDuration);

	switch (Action)
	{
	case EYUFSAction::SeekInformation: return Config->SeekInformationDurationRange;
	case EYUFSAction::WaitForInfo: return Config->WaitAndObserveDurationRange;
	case EYUFSAction::GatherBelongings: return Config->GatherBelongingsDurationRange;
	case EYUFSAction::AlertNearbyOccupants: return Config->AlertAndHelpDurationRange;
	case EYUFSAction::AttemptInitialFirefighting: return Config->InitialFirefightingDurationRange;
	default: return FVector2D(MinActionHoldDuration, MinActionHoldDuration);
	}
}

bool AYUFSEvacuationNPC::IsCurrentRouteUnsafe() const
{
	if (!LevelDataMgr || SelectedRouteStrategy == EYUFSRouteStrategy::ShelterInPlace) return false;
	const FVector Target = ResolveNavigationTarget(CurrentAction);
	return Target.IsZero() || LevelDataMgr->IsLocationDangerous(Target, GetCurrentSimFrame());
}

bool AYUFSEvacuationNPC::IsPreEvacuationState(EYUFSBehaviorState State)
{
	return State == EYUFSBehaviorState::Perceiving
		|| State == EYUFSBehaviorState::Milling
		|| State == EYUFSBehaviorState::RiskAssessment
		|| State == EYUFSBehaviorState::Preparing;
}

void AYUFSEvacuationNPC::OnActionChanged(EYUFSAction NewAction)
{
	LookAnchorYaw = GetActorRotation().Yaw;
	LookElapsed   = 0.f;

	if (!IsNavigationAction(NewAction))
	{
		if (Navigator) Navigator->ClearPath();
		if (UCharacterMovementComponent* Mv = GetCharacterMovement())
			Mv->StopMovementImmediately();
		CurrentNavTarget = FVector::ZeroVector;
	}
	else
	{
		if (UCharacterMovementComponent* Mv = GetCharacterMovement())
			if (Mv->MaxWalkSpeed < 1.f) Mv->MaxWalkSpeed = 400.f;

		const FVector Target = ResolveNavigationTarget(NewAction);
		if (!Target.IsZero() && Navigator && !Navigator->bIsPathfinding)
		{
			CurrentNavTarget = Target;
			Navigator->ClearPath();
			Navigator->RequestPathAsync(Target, GetCurrentSimFrame());
		}
	}
}

void AYUFSEvacuationNPC::ExecuteCurrentAction(float DeltaTime)
{
	if (!BehaviorSM || BehaviorSM->IsIncapacitated()) return;

	switch (CurrentAction)
	{
	case EYUFSAction::SeekInformation:
	case EYUFSAction::AlertNearbyOccupants:
		if (BehaviorSM->Config)
		{
			LookElapsed += DeltaTime;
			const float Osc = FMath::Sin(LookElapsed * 2.f * PI * BehaviorSM->Config->LookAroundFrequencyHz);
			FRotator Rot = GetActorRotation();
			Rot.Yaw = LookAnchorYaw + Osc * BehaviorSM->Config->LookAroundYawAmplitudeDegrees;
			SetActorRotation(Rot);
		}
		break;

	case EYUFSAction::Cough:
		if (CoughMontage)
		{
			UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
			if (Anim && !Anim->Montage_IsPlaying(CoughMontage))
				PlayAnimMontage(CoughMontage);
		}
		break;

	case EYUFSAction::Film:
		if (FilmMontage)
		{
			UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
			if (Anim && !Anim->Montage_IsPlaying(FilmMontage))
				PlayAnimMontage(FilmMontage);
		}
		break;

	case EYUFSAction::AttemptInitialFirefighting:
		if (InitialFirefightingMontage)
		{
			UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
			if (Anim && !Anim->Montage_IsPlaying(InitialFirefightingMontage))
				PlayAnimMontage(InitialFirefightingMontage);
		}
		break;

	case EYUFSAction::EvacuateToNearestExit:
	case EYUFSAction::EvacuateToFamiliarExit:
	case EYUFSAction::FollowCrowd:
	case EYUFSAction::HelpOther:
		if (Navigator)
		{
			const FVector Target = ResolveNavigationTarget(CurrentAction);
			if (Target.IsZero()) break;

			Navigator->CheckAndReroute(GetCurrentSimFrame());

			if (!Navigator->bIsPathfinding && FVector::Dist(Target, CurrentNavTarget) > 500.f)
			{
				CurrentNavTarget = Target;
				Navigator->ClearPath();
				Navigator->RequestPathAsync(Target, GetCurrentSimFrame());
			}

			if (!Navigator->bIsPathfinding && !Navigator->GetCurrentPathPoints().IsEmpty())
				DriveMovementToward(Target);
		}
		break;

	default:
		// Idle / WaitForInfo / GatherBelongings — 이동 없음
		break;
	}
}

FVector AYUFSEvacuationNPC::ResolveNavigationTarget(EYUFSAction Action) const
{
	if (!LevelDataMgr) return FVector::ZeroVector;
	const FVector Pos   = GetActorLocation();
	const int32   Frame = GetCurrentSimFrame();

	switch (Action)
	{
	case EYUFSAction::EvacuateToNearestExit:
		if (bReceivedStaffGuidance && !StaffGuidedExitLocation.IsZero())
		{
			if (!LevelDataMgr->IsLocationDangerous(StaffGuidedExitLocation, Frame))
				return StaffGuidedExitLocation;
		}
		{
			FVector SafeExit = FVector::ZeroVector;
			return LevelDataMgr->TryGetNearestSafeExit(Pos, Frame, SafeExit)
				? SafeExit
				: FVector::ZeroVector;
		}

	case EYUFSAction::EvacuateToFamiliarExit:
		{
			const FVector FamiliarExit = LevelDataMgr->GetFamiliarExit(SpawnLocation);
			if (!LevelDataMgr->IsLocationDangerous(FamiliarExit, Frame)) return FamiliarExit;

			FVector SafeExit = FVector::ZeroVector;
			return LevelDataMgr->TryGetNearestSafeExit(Pos, Frame, SafeExit)
				? SafeExit
				: FVector::ZeroVector;
		}

	case EYUFSAction::FollowCrowd:
		if (SocialComp)
		{
			const FVector Avg = SocialComp->GetAverageEvacuationDestination();
			if (!Avg.IsZero() && !LevelDataMgr->IsLocationDangerous(Avg, Frame)) return Avg;
		}
		{
			FVector SafeExit = FVector::ZeroVector;
			return LevelDataMgr->TryGetNearestSafeExit(Pos, Frame, SafeExit)
				? SafeExit
				: FVector::ZeroVector;
		}

	case EYUFSAction::HelpOther:
		if (SocialComp)
		{
			const FVector HelpLoc = SocialComp->GetNearestNPCNeedingHelpLocation();
			if (!HelpLoc.IsZero()) return HelpLoc;
		}
		return FVector::ZeroVector;

	default:
		return FVector::ZeroVector;
	}
}

bool AYUFSEvacuationNPC::IsNavigationAction(EYUFSAction Action)
{
	return Action == EYUFSAction::EvacuateToNearestExit
		|| Action == EYUFSAction::EvacuateToFamiliarExit
		|| Action == EYUFSAction::FollowCrowd
		|| Action == EYUFSAction::HelpOther;
}
