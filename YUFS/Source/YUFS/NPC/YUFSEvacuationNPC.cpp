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
#include "Core/YUFSRewardCalculator.h"
#include "Debug/YUFSNPCDebugComponent.h"
#include "EngineUtils.h"
#include "Fire/YUFSBinaryManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Level/YUFSLevelDataManager.h"
#include "Navigation/YUFSSmokeAwareNavigator.h"
#include "NavigationSystem.h"
#include "Perception/YUFSNPCPerceptionComponent.h"
#include "Simulation/YUFSBottleneckQueueManager.h"
#include "Simulation/YUFSSimulationController.h"
#include "Social/YUFSSocialInfluenceComponent.h"

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

	if (AYUFSEmergencyCommSystem* CommSystem = Cast<AYUFSEmergencyCommSystem>(
		UGameplayStatics::GetActorOfClass(GetWorld(), AYUFSEmergencyCommSystem::StaticClass())))
	{
		CommSystem->OnEmergencyComm.AddDynamic(this, &AYUFSEvacuationNPC::OnCommReceived);
	}

	for (TActorIterator<AYUFSBinaryManager> It(GetWorld()); It; ++It)  { BinaryManager = *It; break; }
	for (TActorIterator<AYUFSLevelDataManager> It(GetWorld()); It; ++It){ LevelDataMgr  = *It; break; }
	for (TActorIterator<AYUFSBottleneckQueueManager> It(GetWorld()); It; ++It){ BottleneckQueueManager = *It; break; }

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

	// ── 시뮬레이션 일시정지 ───────────────────────────────────────────────
	if (SimulationController && !SimulationController->IsNPCSimulationEnabled())
	{
		if (BottleneckQueueManager) BottleneckQueueManager->ReleaseRequester(this);
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

	// 병목 대기열 해소 — 경로 포인트 기반으로 임시 중간 목적지 선택
	FVector NavTarget = Target;
	if (BottleneckQueueManager)
	{
		const FVector Resolved = BottleneckQueueManager->ResolveMovementTarget(
			this, Navigator->GetCurrentPathPoints(), Target);
		if (!Resolved.IsZero()) NavTarget = Resolved;
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

void AYUFSEvacuationNPC::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
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
	const FVector Shelt = LevelDataMgr->GetNearestAvailableShelter(Pos);

	Out.DistToNearestExit    = FVector::Dist(Pos, NExit);
	Out.DistToFamiliarExit   = FVector::Dist(Pos, FExit);
	Out.DistToNearestShelter = FVector::Dist(Pos, Shelt);
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

void AYUFSEvacuationNPC::NotifyEpisodeFinished(EYUFSTerminalReason TerminalReason)
{
	if (BottleneckQueueManager) BottleneckQueueManager->ReleaseRequester(this);

	if (!bHasPendingTransition) return;

	FYUFSNPCObservation TerminalObs{};
	BuildObservation(TerminalObs);
	FlushLearningTransition(TerminalObs, TerminalReason);
}

float AYUFSEvacuationNPC::CalculateTransitionReward(
	const FYUFSNPCObservation& PrevObs, EYUFSAction Action,
	const FYUFSNPCObservation& NextObs, EYUFSTerminalReason TerminalReason) const
{
	return YUFSRewardCalculator::Calculate(PrevObs, Action, NextObs, TerminalReason);
}

void AYUFSEvacuationNPC::FlushLearningTransition(const FYUFSNPCObservation& NextObs, EYUFSTerminalReason TerminalReason)
{
	if (!bHasPendingTransition || !bLogTransitions) return;

	const float Reward = CalculateTransitionReward(PrevObservation, CurrentAction, NextObs, TerminalReason);
	const bool bDone = TerminalReason != EYUFSTerminalReason::None;

	FYUFSExperienceLogger::LogTransition(
		GetName(),
		SimulationController ? SimulationController->GetCurrentRunIndex() : 0,
		TransitionStepIndex,
		GetCurrentSimFrame(),
		SimulationController ? SimulationController->GetElapsedTime() : 0.f,
		PrevObservation,
		CurrentAction,
		Reward,
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

	MLPolicy.SetLearningMode(bLearningMode);

	ActionHoldTimer         += DeltaTime;
	PolicyTickAccumulator   += DeltaTime;

	if (PolicyTickAccumulator >= PolicyTickInterval)
	{
		PolicyTickAccumulator = 0.f;

		FYUFSNPCObservation Obs{};
		BuildObservation(Obs);
		const EYUFSAction NewAction = MLPolicy.SelectAction(Obs);

		// PADM 상태 전이가 발생하면 즉시 반응, 아니면 최소 유지 시간 보장
		// (RuleBasedPolicy의 FMath::FRand() 매 틱 재추첨으로 인한 떨림 방지)
		const bool bStateChanged   = Obs.CurrentState != LastPolicyBehaviorState;
		const bool bHeldLongEnough = ActionHoldTimer >= MinActionHoldDuration;

		if (NewAction != CurrentAction && (bStateChanged || bHeldLongEnough))
		{
			ActionHoldTimer = 0.f;
			OnActionChanged(NewAction);
			CurrentAction = NewAction;
		}

		LastPolicyBehaviorState = Obs.CurrentState;
	}

	ExecuteCurrentAction(DeltaTime);
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

	case EYUFSAction::EvacuateToNearestExit:
	case EYUFSAction::EvacuateToFamiliarExit:
	case EYUFSAction::FollowCrowd:
	case EYUFSAction::MoveToShelter:
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
			return StaffGuidedExitLocation;
		return LevelDataMgr->GetNearestSafeExit(Pos, true, Frame);

	case EYUFSAction::EvacuateToFamiliarExit:
		return LevelDataMgr->GetFamiliarExit(SpawnLocation);

	case EYUFSAction::FollowCrowd:
		if (SocialComp)
		{
			const FVector Avg = SocialComp->GetAverageEvacuationDestination();
			if (!Avg.IsZero()) return Avg;
		}
		return LevelDataMgr->GetNearestSafeExit(Pos, true, Frame);

	case EYUFSAction::MoveToShelter:
		return LevelDataMgr->GetNearestAvailableShelter(Pos);

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
		|| Action == EYUFSAction::MoveToShelter
		|| Action == EYUFSAction::HelpOther;
}
