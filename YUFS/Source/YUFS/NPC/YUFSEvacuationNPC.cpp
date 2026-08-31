#include "NPC/YUFSEvacuationNPC.h"

#include "AIController.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "NPC/Behavior/YUFSBehaviorConfig.h"
#include "NPC/Animation/YUFSActionAnimationComponent.h"
#include "Communication/YUFSCommTypes.h"
#include "Communication/YUFSEmergencyCommSystem.h"
#include "Animation/AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/YUFSDecisionTraceLogger.h"
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
#include "NPC/Decision/YUFSBeliefComponent.h"
#include "NPC/Decision/YUFSIntentComponent.h"
#include "NPC/Tasks/YUFSActionTaskComponent.h"
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
	BeliefComp     = CreateDefaultSubobject<UYUFSBeliefComponent>(TEXT("YUFSBeliefComponent"));
	IntentComp     = CreateDefaultSubobject<UYUFSIntentComponent>(TEXT("YUFSIntentComponent"));
	ActionTaskComp = CreateDefaultSubobject<UYUFSActionTaskComponent>(TEXT("YUFSActionTaskComponent"));
	ActionAnimationComp = CreateDefaultSubobject<UYUFSActionAnimationComponent>(TEXT("YUFSActionAnimationComponent"));

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

	if (StableNPCId == INDEX_NONE)
	{
		StableNPCId = static_cast<int32>(FCrc::StrCrc32(*GetPathName()) & 0x7fffffffu);
	}
	DeterministicRng.Initialize(ScenarioSeed, StableNPCId);
	MLPolicy.SetFallbackRandomSource(&DeterministicRng);
	if (ActionAnimationComp)
	{
		ActionAnimationComp->Initialize(GetMesh(), StableNPCId);
	}

	// 첫 갱신 시점을 NPC마다 분산해 대규모 스폰 시 Trace/Overlap 피크를 방지한다.
	const float PerceptionInterval = FMath::Max(PerceptionUpdateIntervalSeconds, 0.05f);
	const float SocialInterval = FMath::Max(SocialUpdateIntervalSeconds, 0.05f);
	const float UniquePhase = static_cast<float>(StableNPCId % 1000);
	PerceptionUpdateAccumulator = FMath::Fmod(UniquePhase * 0.61803398875f, PerceptionInterval);
	SocialUpdateAccumulator = FMath::Fmod(UniquePhase * 0.38196601125f, SocialInterval);

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
		UpdateActionAnimation();
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
		UpdateActionAnimation();
		if (Navigator) Navigator->ClearPath();
		if (UCharacterMovementComponent* Mv = GetCharacterMovement())
		{
			Mv->StopMovementImmediately();
			Mv->MaxWalkSpeed = 0.f;
		}
		return;
	}

	const int32 CurrentFrame = GetCurrentSimFrame();

	// ── 지각 / 사회 갱신 ────────────────────────────────────────────────
	// 감지는 5Hz, 근접 NPC 탐색은 5Hz가 기본값이다. 각 NPC의 시작 위상을
	// 분산했으므로 같은 프레임에 모든 NPC가 물리 쿼리를 실행하지 않는다.
	PerceptionUpdateAccumulator += DeltaTime;
	const float PerceptionInterval = FMath::Max(PerceptionUpdateIntervalSeconds, 0.05f);
	if (PerceptionComp && PerceptionUpdateAccumulator >= PerceptionInterval)
	{
		PerceptionUpdateAccumulator = FMath::Fmod(PerceptionUpdateAccumulator, PerceptionInterval);
		PerceptionComp->UpdatePerception(CurrentFrame);
	}

	SocialUpdateAccumulator += DeltaTime;
	const float SocialInterval = FMath::Max(SocialUpdateIntervalSeconds, 0.05f);
	if (SocialComp && SocialUpdateAccumulator >= SocialInterval)
	{
		SocialUpdateAccumulator = FMath::Fmod(SocialUpdateAccumulator, SocialInterval);
		SocialComp->UpdateSocialContext();
	}

	// ── Observation은 Tick당 한 번만 생성해 상태머신/정책/기록이 공유한다. ──
	FYUFSNPCObservation CurrentObs{};
	BuildObservation(CurrentObs);

	// ── PADM 상태머신 갱신 ───────────────────────────────────────────────
	if (BehaviorSM)
	{
		BehaviorSM->TickStateMachine(DeltaTime, CurrentObs);
		// 상태머신이 이번 Tick에 갱신한 상태를 정책과 기록에 반영한다.
		CurrentObs.CurrentState = BehaviorSM->GetCurrentState();
		CurrentObs.RiskPerception = BehaviorSM->GetRiskPerception();
		CurrentObs.SmokeExposureAccumulated = BehaviorSM->GetSmokeExposure();
	}

	// ── 근거 기반 Belief → Intent 갱신 및 V1 상태 투영 ────────────────
	UpdateEvidenceDecisionModel(DeltaTime, CurrentObs);

	// ── MLP 정책 추론 및 액션 실행 ───────────────────────────────────────
	TickPolicy(DeltaTime, CurrentObs);

	if (bEnableEvidenceDecisionModel && ActionTaskComp && BeliefComp && IntentComp)
	{
		ActionTaskComp->UpdateTask(
			DeltaTime,
			CurrentAction,
			IntentComp->GetCurrentIntent(),
			BeliefComp->HasImmediateLifeRisk(),
			BeliefComp->HasVerifiedOfficialInstruction(),
			DeterministicRng);

		EYUFSActionTask FromTask = EYUFSActionTask::None;
		EYUFSActionTask ToTask = EYUFSActionTask::None;
		EYUFSTaskCancelReason Reason = EYUFSTaskCancelReason::None;
		while (ActionTaskComp->ConsumeTaskEvent(FromTask, ToTask, Reason))
		{
			const EYUFSActionTask EventTask = ToTask != EYUFSActionTask::None ? ToTask : FromTask;
			TraceTaskEvent(EventTask, Reason, ToTask != EYUFSActionTask::None ? TEXT("TaskStarted") : TEXT("TaskEnded"));
			if (Reason == EYUFSTaskCancelReason::Completed)
			{
				IntentComp->NotifyPreActionCompleted(bHasSafeExit);
				BehaviorSM->ApplyIntentProjection(IntentComp->GetCurrentIntent());
				if (IntentComp->DidIntentChange())
				{
					TraceIntentTransition();
				}
				ActionHoldTimer = MinActionHoldDuration;
			}
		}
	}
	CurrentObs.MillingActionCount = MillingActionCount;

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

	// ── CSV 로깅 (최대 10Hz) ───────────────────────────────────────────────
	const EYUFSTerminalReason TerminalReason = GetCurrentTerminalReason();
	TransitionLogAccumulator += DeltaTime;
	const float LogInterval = FMath::Max(TransitionLogIntervalSeconds, 0.1f);

	if (!bHasPendingTransition)
	{
		// 첫 관찰값은 기준 상태로만 보관한다.
		PrevObservation = CurrentObs;
		bHasPendingTransition = bLogTransitions;
		TransitionLogAccumulator = 0.f;
	}
	else if (TerminalReason != EYUFSTerminalReason::None || TransitionLogAccumulator >= LogInterval)
	{
		FlushLearningTransition(CurrentObs, TerminalReason);
		TransitionLogAccumulator = FMath::Fmod(TransitionLogAccumulator, LogInterval);
		if (bHasPendingTransition)
		{
			PrevObservation = CurrentObs;
		}
	}

	if (TerminalReason != EYUFSTerminalReason::None)
	{
		if (UCharacterMovementComponent* Mv = GetCharacterMovement()) Mv->MaxWalkSpeed = 0.f;
		if (Navigator) Navigator->ClearPath();
	}

	UpdateActionAnimation();

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
	FVector NExit = FVector::ZeroVector;
	const bool bFoundSafeExit = LevelDataMgr->TryGetNearestSafeExit(Pos, Frame, NExit);
	const FVector FExit = LevelDataMgr->GetFamiliarExit(SpawnLocation);

	Out.DistToNearestExit    = bFoundSafeExit ? FVector::Dist(Pos, NExit) : 100000.f;
	Out.DistToFamiliarExit   = FVector::Dist(Pos, FExit);
	Out.DirToNearestExit     = bFoundSafeExit ? (NExit - Pos).GetSafeNormal() : FVector::ZeroVector;
	Out.SimTimeNormalized    = FMath::Clamp(static_cast<float>(Frame) / 8000.f, 0.f, 1.f);
	Out.bNearestExitSmokeFree= bFoundSafeExit;
}

void AYUFSEvacuationNPC::UpdateEvidenceDecisionModel(float DeltaTime, FYUFSNPCObservation& Observation)
{
	if (!bEnableEvidenceDecisionModel || !BeliefComp || !IntentComp || !BehaviorSM)
	{
		return;
	}

	BeliefComp->UpdateBelief(Observation);
	bHasSafeExit = LevelDataMgr && LevelDataMgr->TryGetNearestSafeExit(
		GetActorLocation(),
		GetCurrentSimFrame(),
		LastSafeExit);
	if (!bHasSafeExit)
	{
		LastSafeExit = FVector::ZeroVector;
	}

	IntentComp->UpdateIntent(DeltaTime, Observation, *BeliefComp, bHasSafeExit, DeterministicRng);
	BehaviorSM->ApplyIntentProjection(IntentComp->GetCurrentIntent());

	// 기존 UI/ONNX V1은 호환 투영된 BehaviorState를 계속 읽는다.
	Observation.CurrentState = BehaviorSM->GetCurrentState();
	Observation.RiskPerception = BehaviorSM->GetRiskPerception();
	Observation.SmokeExposureAccumulated = BehaviorSM->GetSmokeExposure();

	if (IntentComp->DidIntentChange())
	{
		TraceIntentTransition();
	}
}

EYUFSIntent AYUFSEvacuationNPC::GetCurrentIntent() const
{
	return IntentComp ? IntentComp->GetCurrentIntent() : EYUFSIntent::Observe;
}

bool AYUFSEvacuationNPC::RollSocialProbability(float Probability)
{
	return DeterministicRng.IsInitialized()
		? DeterministicRng.Roll(EYUFSRngStream::Social, Probability)
		: Probability >= 0.5f;
}

void AYUFSEvacuationNPC::ApplyDistributedSpawnLocation(const FVector& NewLocation)
{
	SetActorLocation(NewLocation, false, nullptr, ETeleportType::TeleportPhysics);
	SpawnLocation = NewLocation;
	LastMovementSampleLocation = NewLocation;
	LastPositionCheckLocation = NewLocation;
	bHasMovementSample = true;
	CurrentNavTarget = FVector::ZeroVector;

	if (Navigator)
	{
		Navigator->ClearPath();
	}
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
	}
}

void AYUFSEvacuationNPC::TraceIntentTransition() const
{
	if (!bLogDecisionTrace || !IntentComp || !BeliefComp)
	{
		return;
	}

	FYUFSDecisionTraceLogger::LogEvent(
		SimulationController ? SimulationController->GetCurrentRunIndex() : 0,
		StableNPCId,
		GetCurrentSimFrame(),
		SimulationController ? SimulationController->GetElapsedTime() : 0.f,
		BeliefComp->GetPolicyHash(),
		GetScenarioHash(),
		IntentComp->GetDecisionIndex(),
		IntentComp->GetPreviousIntent(),
		IntentComp->GetCurrentIntent(),
		IntentComp->GetLastTrigger(),
		BeliefComp->GetCommitProbability(),
		BeliefComp->GetActiveCueMask(),
		bHasSafeExit,
		ActionTaskComp ? ActionTaskComp->GetCurrentTask() : EYUFSActionTask::None,
		EYUFSTaskCancelReason::None,
		IntentComp->GetPreActionCompletedCount(),
		IntentComp->GetPreActionTargetCount(),
		DeterministicRng.GetDrawCount(EYUFSRngStream::Decision),
		DeterministicRng.GetDrawCount(EYUFSRngStream::TaskDuration),
		DeterministicRng.GetDrawCount(EYUFSRngStream::Route),
		DeterministicRng.GetDrawCount(EYUFSRngStream::Social));
}

void AYUFSEvacuationNPC::TraceTaskEvent(
	EYUFSActionTask Task,
	EYUFSTaskCancelReason Reason,
	const FString& Trigger) const
{
	if (!bLogDecisionTrace || !IntentComp || !BeliefComp)
	{
		return;
	}

	FYUFSDecisionTraceLogger::LogEvent(
		SimulationController ? SimulationController->GetCurrentRunIndex() : 0,
		StableNPCId,
		GetCurrentSimFrame(),
		SimulationController ? SimulationController->GetElapsedTime() : 0.f,
		BeliefComp->GetPolicyHash(),
		GetScenarioHash(),
		IntentComp->GetDecisionIndex(),
		IntentComp->GetCurrentIntent(),
		IntentComp->GetCurrentIntent(),
		Trigger,
		BeliefComp->GetCommitProbability(),
		BeliefComp->GetActiveCueMask(),
		bHasSafeExit,
		Task,
		Reason,
		IntentComp->GetPreActionCompletedCount(),
		IntentComp->GetPreActionTargetCount(),
		DeterministicRng.GetDrawCount(EYUFSRngStream::Decision),
		DeterministicRng.GetDrawCount(EYUFSRngStream::TaskDuration),
		DeterministicRng.GetDrawCount(EYUFSRngStream::Route),
		DeterministicRng.GetDrawCount(EYUFSRngStream::Social));
}

FString AYUFSEvacuationNPC::GetScenarioHash() const
{
	const FString MapName = GetWorld() ? GetWorld()->GetMapName() : TEXT("NoWorld");
	const FString Canonical = FString::Printf(TEXT("%s|seed=%d"), *MapName, ScenarioSeed);
	return FString::Printf(TEXT("crc32:%08x"), FCrc::StrCrc32(*Canonical));
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
	Snapshot.Intent = IntentComp ? IntentComp->GetCurrentIntent() : EYUFSIntent::Observe;
	Snapshot.ActionTask = ActionTaskComp ? ActionTaskComp->GetCurrentTask() : EYUFSActionTask::None;
	Snapshot.CommitProbability = BeliefComp ? BeliefComp->GetCommitProbability() : 0.f;
	Snapshot.PreActionCompletedCount = IntentComp ? IntentComp->GetPreActionCompletedCount() : 0;
	Snapshot.PreActionTargetCount = IntentComp ? IntentComp->GetPreActionTargetCount() : 0;
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
	// Transform/가시성 변경은 렌더 프록시와 Ray Tracing Scene 갱신을 유발하므로,
	// 스냅샷이 현재 상태와 실제로 다를 때만 적용합니다.
	constexpr float LocationToleranceCm = 1.0f;
	constexpr float RotationToleranceDeg = 0.1f;
	if (!GetActorLocation().Equals(Snapshot.Location, LocationToleranceCm) ||
		!GetActorRotation().Equals(Snapshot.Rotation, RotationToleranceDeg))
	{
		SetActorLocationAndRotation(Snapshot.Location, Snapshot.Rotation, false, nullptr, ETeleportType::TeleportPhysics);
	}

	const bool bShouldBeHidden = !Snapshot.bVisible;
	if (IsHidden() != bShouldBeHidden)
	{
		SetActorHiddenInGame(bShouldBeHidden);
	}

	if (GetActorEnableCollision() != Snapshot.bVisible)
	{
		SetActorEnableCollision(Snapshot.bVisible);
	}

	CurrentAction = Snapshot.CurrentAction;
	UpdateActionAnimation();

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

void AYUFSEvacuationNPC::TickPolicy(float DeltaTime, const FYUFSNPCObservation& Observation)
{
	if (!BehaviorSM) return;
	if (BehaviorSM->IsIncapacitated()) return;

	MLPolicy.SetDataCollectionMode(bDataCollectionMode);

	ActionHoldTimer         += DeltaTime;
	PolicyTickAccumulator   += DeltaTime;

	if (PolicyTickAccumulator >= PolicyTickInterval)
	{
		PolicyTickAccumulator = 0.f;

		const EYUFSAction NewAction = ConstrainActionForIntent(MLPolicy.SelectAction(Observation));

		if (Observation.CurrentState == EYUFSBehaviorState::Milling)
			++MillingActionCount;

		// PADM 상태 전이가 발생하면 즉시 반응, 아니면 최소 유지 시간 보장
		// (RuleBasedPolicy의 FMath::FRand() 매 틱 재추첨으로 인한 떨림 방지)
		const bool bStateChanged   = Observation.CurrentState != LastPolicyBehaviorState;
		const bool bHeldLongEnough = ActionHoldTimer >= MinActionHoldDuration;

		if (NewAction != CurrentAction && (bStateChanged || bHeldLongEnough))
		{
			ActionHoldTimer = 0.f;
			OnActionChanged(NewAction);
			CurrentAction = NewAction;
		}

		LastPolicyBehaviorState = Observation.CurrentState;
	}

	ExecuteCurrentAction(DeltaTime);
}

EYUFSAction AYUFSEvacuationNPC::ConstrainActionForIntent(EYUFSAction ProposedAction) const
{
	if (!bEnableEvidenceDecisionModel || !IntentComp)
	{
		return ProposedAction;
	}

	switch (IntentComp->GetCurrentIntent())
	{
	case EYUFSIntent::Incapacitated:
		return EYUFSAction::Cough;
	case EYUFSIntent::Shelter:
	case EYUFSIntent::Reenter:
		return EYUFSAction::WaitForInfo;
	case EYUFSIntent::Help:
		return EYUFSAction::HelpOther;
	case EYUFSIntent::CommitEvac:
		if (!bHasSafeExit)
		{
			return EYUFSAction::WaitForInfo;
		}
		return IsNavigationAction(ProposedAction) && ProposedAction != EYUFSAction::HelpOther
			? ProposedAction
			: EYUFSAction::EvacuateToNearestExit;
	case EYUFSIntent::Observe:
		return IsNavigationAction(ProposedAction) ? EYUFSAction::SeekInformation : ProposedAction;
	case EYUFSIntent::Prepare:
		return IsNavigationAction(ProposedAction) ? EYUFSAction::GatherBelongings : ProposedAction;
	default:
		return ProposedAction;
	}
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

	if (!bActionAnimationPreviewActive && ActionAnimationComp)
	{
		const EYUFSBehaviorState State = BehaviorSM
			? BehaviorSM->GetCurrentState()
			: EYUFSBehaviorState::Normal;
		ActionAnimationComp->ApplyAction(NewAction, State);
	}
}

void AYUFSEvacuationNPC::UpdateActionAnimation(bool bForce)
{
	if (!ActionAnimationComp)
	{
		return;
	}

	const EYUFSAction DisplayAction = GetDisplayedAction();
	const EYUFSBehaviorState DisplayState = bActionAnimationPreviewActive
		? EYUFSBehaviorState::Normal
		: (BehaviorSM ? BehaviorSM->GetCurrentState() : EYUFSBehaviorState::Normal);
	ActionAnimationComp->ApplyAction(DisplayAction, DisplayState, bForce);
}

void AYUFSEvacuationNPC::SetActionAnimationPreview(EYUFSAction Action)
{
	bActionAnimationPreviewActive = true;
	PreviewAction = Action;
	UpdateActionAnimation(true);
}

void AYUFSEvacuationNPC::ClearActionAnimationPreview()
{
	if (!bActionAnimationPreviewActive)
	{
		return;
	}

	bActionAnimationPreviewActive = false;
	PreviewAction = EYUFSAction::Idle;
	UpdateActionAnimation(true);
}

void AYUFSEvacuationNPC::SetAnimationShowcaseDebugSuppressed(bool bSuppressed)
{
	if (DebugComp)
	{
		DebugComp->SetTemporarilySuppressed(bSuppressed);
	}
}

FString AYUFSEvacuationNPC::GetCurrentActionAnimationName() const
{
	return ActionAnimationComp ? ActionAnimationComp->GetActiveAnimationName() : TEXT("None");
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
		if (!ActionAnimationComp && CoughMontage)
		{
			UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
			if (Anim && !Anim->Montage_IsPlaying(CoughMontage))
				PlayAnimMontage(CoughMontage);
		}
		break;

	case EYUFSAction::Film:
		if (!ActionAnimationComp && FilmMontage)
		{
			UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
			if (Anim && !Anim->Montage_IsPlaying(FilmMontage))
				PlayAnimMontage(FilmMontage);
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
	FVector SafeExit = FVector::ZeroVector;
	const bool bFoundSafeExit = LevelDataMgr->TryGetNearestSafeExit(Pos, Frame, SafeExit);

	switch (Action)
	{
	case EYUFSAction::EvacuateToNearestExit:
		if (bReceivedStaffGuidance && !StaffGuidedExitLocation.IsZero()
			&& !LevelDataMgr->IsLocationDangerous(StaffGuidedExitLocation, Frame))
			return StaffGuidedExitLocation;
		return bFoundSafeExit ? SafeExit : FVector::ZeroVector;

	case EYUFSAction::EvacuateToFamiliarExit:
	{
		const FVector FamiliarExit = LevelDataMgr->GetFamiliarExit(SpawnLocation);
		return !LevelDataMgr->IsLocationDangerous(FamiliarExit, Frame)
			? FamiliarExit
			: (bFoundSafeExit ? SafeExit : FVector::ZeroVector);
	}

	case EYUFSAction::FollowCrowd:
		if (SocialComp)
		{
			const FVector Avg = SocialComp->GetAverageEvacuationDestination();
			if (!Avg.IsZero() && !LevelDataMgr->IsLocationDangerous(Avg, Frame)) return Avg;
		}
		return bFoundSafeExit ? SafeExit : FVector::ZeroVector;

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
