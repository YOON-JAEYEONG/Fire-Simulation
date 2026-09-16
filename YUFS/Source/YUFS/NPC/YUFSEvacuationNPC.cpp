#include "NPC/YUFSEvacuationNPC.h"

#include "AIController.h"
#include "NPC/Navigation/YUFSLocalMovementComponent.h"
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
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "NPC/Decision/YUFSBeliefComponent.h"
#include "NPC/Decision/YUFSHumanBehaviorSelectorComponent.h"
#include "NPC/Decision/YUFSIntentComponent.h"
#include "NPC/Cognition/YUFSHumanCognitionComponent.h"
#include "NPC/Integration/YUFSTeamIntegrationComponent.h"
#include "NPC/Integration/YUFSNpcSuppressionComponent.h"
#include "NPC/Integration/YUFSNpcEnvironmentInteraction.h"
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
	LocalMovement = CreateDefaultSubobject<UYUFSLocalMovementComponent>(TEXT("YUFSLocalMovement"));
	BeliefComp     = CreateDefaultSubobject<UYUFSBeliefComponent>(TEXT("YUFSBeliefComponent"));
	IntentComp     = CreateDefaultSubobject<UYUFSIntentComponent>(TEXT("YUFSIntentComponent"));
	ActionTaskComp = CreateDefaultSubobject<UYUFSActionTaskComponent>(TEXT("YUFSActionTaskComponent"));
	ActionAnimationComp = CreateDefaultSubobject<UYUFSActionAnimationComponent>(TEXT("YUFSActionAnimationComponent"));
	HumanCognitionComp = CreateDefaultSubobject<UYUFSHumanCognitionComponent>(TEXT("YUFSHumanCognitionComponent"));
	HumanBehaviorSelector = CreateDefaultSubobject<UYUFSHumanBehaviorSelectorComponent>(TEXT("YUFSHumanBehaviorSelectorComponent"));
	TeamIntegrationComp = CreateDefaultSubobject<UYUFSTeamIntegrationComponent>(TEXT("YUFSTeamIntegrationComponent"));
	SuppressionComp = CreateDefaultSubobject<UYUFSNpcSuppressionComponent>(TEXT("NpcSuppression"));
	EnvironmentInteraction = CreateDefaultSubobject<UYUFSNpcEnvironmentInteraction>(TEXT("EnvironmentInteraction"));

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
	ResetRetreatKnowledge();
	if (Navigator) Navigator->ResetObservedHazards();
	if (SuppressionComp) SuppressionComp->ResetForEpisode();
	SpawnLocation = GetActorLocation();
	BaseWalkSpeed = FMath::Max(200.f,GetCharacterMovement()->MaxWalkSpeed);
	LastMovementSampleLocation = SpawnLocation;
	bHasMovementSample = true;
	EverydayRoamOrigin = SpawnLocation;
	EverydayLookAnchorYaw = GetActorRotation().Yaw;
	EverydayRandomStream.Initialize(EverydayBehaviorSeed ^ static_cast<int32>(GetTypeHash(GetFName())));
	EverydayActivityTimer = EverydayRandomStream.FRandRange(
		FMath::Min(EverydayMinIdleSeconds, EverydayMaxIdleSeconds),
		FMath::Max(EverydayMinIdleSeconds, EverydayMaxIdleSeconds));

	if (StableNPCId == INDEX_NONE)
	{
		StableNPCId = static_cast<int32>(FCrc::StrCrc32(*GetPathName()) & 0x7fffffffu);
	}
	DeterministicRng.Initialize(ScenarioSeed, StableNPCId);
	if (HumanCognitionComp)
	{
		HumanCognitionComp->Initialize(StableNPCId, DeterministicRng);
		if (HumanBehaviorSelector && !HumanBehaviorSelector->PolicyAsset)
		{
			HumanBehaviorSelector->PolicyAsset = HumanCognitionComp->PolicyAsset;
		}
		if (BeliefComp)
		{
			BeliefComp->bTrainingCompleted = BeliefComp->bTrainingCompleted
				|| HumanCognitionComp->GetTraits().FireTraining >= 0.55f;
		}
	}
	MLPolicy.SetFallbackRandomSource(&DeterministicRng);
	if (ActionAnimationComp && !bUseExternalMotionDriver)
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
	bInteractionHoldingPosition = false;
	// ── 타임라인 관찰 모드 ─────────────────────────────────────────────
	// 관찰 모드에서는 AI 판단, 경로 탐색, 이동 입력을 다시 계산하면 안 됩니다.
	// 저장된 스냅샷만 SimulationController/TimelineRecorder가 적용합니다.
	if (bTimelinePlaybackMode)
	{
		StopEverydayBehavior();
		UpdateActionAnimation();
		if (Navigator) Navigator->ClearPath();
		if (LocalMovement) LocalMovement->Reset();
		if (UCharacterMovementComponent* Mv = GetCharacterMovement())
		{
			Mv->StopMovementImmediately();
			Mv->DisableMovement();
		}
		return;
	}

	// 화재 전에는 일상 행동을 허용하고, 일시정지/관찰 모드에서는 멈춘다.
	if (SimulationController && !SimulationController->IsNPCActivityEnabled())
	{
		StopEverydayBehavior();
		// A pause preserves an interaction; leaving the live episode releases it.
		if (SimulationController->GetCurrentPhase() != ESimPhase::FireActive)
		{
			if (SuppressionComp) SuppressionComp->Cancel(false);
			if (EnvironmentInteraction) EnvironmentInteraction->Cancel();
			ResetRetreatKnowledge();
			if (Navigator) Navigator->ResetObservedHazards();
		}
		UpdateActionAnimation();
		if (Navigator) Navigator->ClearPath();
		if (LocalMovement) LocalMovement->Reset();
		if (UCharacterMovementComponent* Mv = GetCharacterMovement())
		{
			Mv->StopMovementImmediately();
			Mv->MaxWalkSpeed = 0.f;
		}
		return;
	}
	if (SuppressionComp && SuppressionComp->IsVisualPresentationActive()
		&& (!SimulationController || !SimulationController->IsNPCSimulationEnabled()))
	{
		StopEverydayBehavior();
		bInteractionHoldingPosition = SuppressionComp->TickVisualPresentation(DeltaTime);
		if (SuppressionComp->IsVisualPresentationActive() && !bInteractionHoldingPosition)
			UpdateNavigationMovement(SuppressionComp->GetMovementTarget(), DeltaTime, 140.f);
		return;
	}
	if (SimulationController)
	{
		const ESimPhase Phase = SimulationController->GetCurrentPhase();
		if (!bInteractionPreviewControlled
			&& (Phase == ESimPhase::WaitingToStart || Phase == ESimPhase::FireStartDelay))
		{
			TickEverydayBehavior(DeltaTime);
			UpdateActionAnimation();
			return;
		}
	}

	if (bInteractionPreviewControlled)
	{
		StopEverydayBehavior();
		EnvironmentInteraction->Observe(DeltaTime);
		FYUFSBehaviorDecision Preview;
		Preview.Behavior = InteractionPreviewBehavior;
		Preview.LegacyAction = InteractionPreviewBehavior == EYUFSHighLevelBehavior::AssistOther ? EYUFSAction::HelpOther
			: InteractionPreviewBehavior == EYUFSHighLevelBehavior::EvacuateNearest ? EYUFSAction::EvacuateToNearestExit : EYUFSAction::WaitForInfo;
		Preview.Reason = TEXT("ExplicitVisualPreview");
		TeamIntegrationComp->PublishDecision(StableNPCId, Preview, EYUFSIntent::CommitEvac, EYUFSBehaviorState::Normal,
			InteractionPreviewDestination, true, FYUFSCognitiveState());
		if (EnvironmentInteraction->IsReceivingContactAssistance())
		{
			GetCharacterMovement()->StopMovementImmediately();
			if (!bUseExternalMotionDriver)
			{
				const auto* Helper = EnvironmentInteraction->GetAssistingNPC();
				SetActorRotation(FRotator(0, (Helper->GetActorLocation()-GetActorLocation()).Rotation().Yaw, 0));
				ActionAnimationComp->ApplyAction(EYUFSAction::AlertNearbyOccupants,EYUFSBehaviorState::Normal);
			}
			return;
		}
		CurrentAction = Preview.LegacyAction;
		ExecuteCurrentAction(DeltaTime);
		if (!EnvironmentInteraction->IsActive())
		{
			if (InteractionPreviewBehavior == EYUFSHighLevelBehavior::EvacuateNearest)
			{
				UpdateNavigationMovement(InteractionPreviewDestination, DeltaTime, 150.f);
				if (!bUseExternalMotionDriver && ActionAnimationComp)
					ActionAnimationComp->ApplyAction(GetVelocity().Size2D()>5.f?EYUFSAction::HelpOther:EYUFSAction::Idle,EYUFSBehaviorState::Normal);
			}
			else if (!bUseExternalMotionDriver && ActionAnimationComp)
				ActionAnimationComp->ApplyAction(EYUFSAction::WaitForInfo,EYUFSBehaviorState::Normal);
		}
		return;
	}

	const int32 CurrentFrame = GetCurrentSimFrame();
	PeerGuidanceSecondsRemaining = FMath::Max(0.f, PeerGuidanceSecondsRemaining - DeltaTime);

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
	ProcessTeamFeedback();

	// ── PADM 상태머신 갱신 ───────────────────────────────────────────────
	if (BehaviorSM)
	{
		BehaviorSM->TickStateMachine(DeltaTime, CurrentObs);
		// 상태머신이 이번 Tick에 갱신한 상태를 정책과 기록에 반영한다.
		CurrentObs.CurrentState = BehaviorSM->GetCurrentState();
		CurrentObs.RiskPerception = BehaviorSM->GetRiskPerception();
		CurrentObs.SmokeExposureAccumulated = BehaviorSM->GetSmokeExposure();
	}

	// Live authored gestures still perceive smoke and update emergency behavior.
	// They must not publish fabricated suppression evidence or learning actions.
	if (SuppressionComp && SuppressionComp->IsVisualPresentationActive())
	{
		LiveObservation = CurrentObs;
		if (SuppressionComp->ShouldInterruptAuthoredGesture(CurrentObs))
			SuppressionComp->Cancel(false);
		else
		{
			StopEverydayBehavior();
			bInteractionHoldingPosition = SuppressionComp->TickVisualPresentation(DeltaTime);
			if (SuppressionComp->IsVisualPresentationActive() && !bInteractionHoldingPosition)
				UpdateNavigationMovement(SuppressionComp->GetMovementTarget(), DeltaTime, 140.f);
			return;
		}
	}
	UpdateEvidenceDecisionModel(DeltaTime, CurrentObs);
	if (SuppressionComp) SuppressionComp->Observe(DeltaTime, CurrentFrame, CurrentObs);
	if (EnvironmentInteraction) EnvironmentInteraction->Observe(DeltaTime);
	LiveObservation = CurrentObs;

	// 단서를 아직 인식하지 못했다면 평상시 산책을 유지한다.
	if (BehaviorSM && BehaviorSM->GetCurrentState() == EYUFSBehaviorState::Normal
		&& (!EnvironmentInteraction || !EnvironmentInteraction->IsReceivingContactAssistance()))
	{
		TickEverydayBehavior(DeltaTime);
	}
	else
	{
		StopEverydayBehavior();
		if (auto* Mv = GetCharacterMovement())
		{
			Mv->MaxWalkSpeed = GetDesiredWalkingSpeed();
			if (LocalMovement->IsRecovering()) Mv->MaxWalkSpeed = FMath::Min(Mv->MaxWalkSpeed, 160.f);
		}
		TickPolicy(DeltaTime, CurrentObs);
	}

	// Task timers are presentation/calibration only; they cannot advance JJW preparation/commitment.
	if (ActionTaskComp && IntentComp)
	{
		const EYUFSActionTask Task = bOptionalInteractionSelected ? EYUFSActionTask::None
			: CurrentAction == EYUFSAction::GatherBelongings ? EYUFSActionTask::GatherBelongings
			: CurrentAction == EYUFSAction::SeekInformation ? EYUFSActionTask::SeekInformation : EYUFSActionTask::None;
		ActionTaskComp->UpdateDesiredTask(DeltaTime, Task, static_cast<int64>(CurrentAction),
			GetCurrentIntent(), BehaviorSM && BehaviorSM->IsIncapacitated(),
			bReceivedStaffGuidance || bReceivedLiveAnnouncement, DeterministicRng);
		EYUFSActionTask From = EYUFSActionTask::None, To = EYUFSActionTask::None;
		EYUFSTaskCancelReason Reason = EYUFSTaskCancelReason::None;
		while (ActionTaskComp->ConsumeTaskEvent(From, To, Reason))
			TraceTaskEvent(To != EYUFSActionTask::None ? To : From, Reason, TEXT("JJWPresentationTask"));
	}
	PublishTeamDirectives(CurrentObs);
	CurrentObs.MillingActionCount = MillingActionCount;
	LiveObservation = CurrentObs;

	// ── 이동 속도 제한 (Crawl / Incapacitated) ───────────────────────────
	if (UCharacterMovementComponent* Mv = GetCharacterMovement())
	{
		if (BehaviorSM && BehaviorSM->Config)
		{
			if (BehaviorSM->IsIncapacitated())
			{
				if (SuppressionComp) SuppressionComp->Cancel(false);
				if (EnvironmentInteraction) EnvironmentInteraction->Cancel();
				Mv->MaxWalkSpeed = 0.f;
				if (Navigator) Navigator->ClearPath();
				if (LocalMovement) LocalMovement->Reset();
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
		if (SuppressionComp) SuppressionComp->Cancel(false);
		if (EnvironmentInteraction) EnvironmentInteraction->Cancel();
		if (LocalMovement) LocalMovement->Reset();
		if (UCharacterMovementComponent* Mv = GetCharacterMovement()) Mv->MaxWalkSpeed = 0.f;
		if (Navigator) Navigator->ClearPath();
	}

	UpdateActionAnimation();

}

void AYUFSEvacuationNPC::DriveMovementToward(FVector Target, float DeltaTime)
{
	if (!Navigator || !Navigator->IsFollowingPath()) return;

	// 일시정지 → 재개 시 MaxWalkSpeed 가 0 으로 남아있는 경우 복원
	// (Crawling 속도 제한은 NPC Tick 에서 별도로 cap 하므로 여기선 무조건 양수만 보장)
	if (UCharacterMovementComponent* Mv = GetCharacterMovement())
	{
		if (Mv->MaxWalkSpeed < 1.f)
			Mv->MaxWalkSpeed = GetDesiredWalkingSpeed();
	}

	Navigator->UpdateWaypoint(GetActorLocation(), 25.f);
	if (!Navigator->IsFollowingPath()) return;
	const FVector SteeringTarget = Navigator->GetSteeringTarget(GetActorLocation(), 120.f);

	FVector Dir = SteeringTarget - GetActorLocation();
	Dir.Z = 0.f;

	if (!Dir.IsNearlyZero(1.f))
	{
		Dir.Normalize();
		Dir=LocalMovement->ResolveDirection(Dir,DeltaTime,GetCurrentSimFrame());
		if (!Dir.IsNearlyZero()) AddMovementInput(Dir,1.f);
	}
}

void AYUFSEvacuationNPC::SetMovementSpeed(float Speed)
{
	BaseWalkSpeed = FMath::Max(0.f,Speed);
	if (UCharacterMovementComponent* Mv = GetCharacterMovement())
		Mv->MaxWalkSpeed = Speed;
}

void AYUFSEvacuationNPC::TickEverydayBehavior(float DeltaTime)
{
	AAIController* AI = Cast<AAIController>(GetController());
	if (!bEnableEverydayBehavior || bUseExternalNavigationDriver || !AI)
	{
		StopEverydayBehavior();
		return;
	}

	if (!bEverydayBehaviorActive)
	{
		bEverydayBehaviorActive = true;
		EverydayLookAnchorYaw = GetActorRotation().Yaw;
		EverydayLookElapsed = 0.f;
	}
	if (UCharacterMovementComponent* Mv = GetCharacterMovement())
	{
		if (Mv->MovementMode == MOVE_None) Mv->SetMovementMode(MOVE_Walking);
		Mv->MaxWalkSpeed = FMath::Max(1.f, EverydayWalkSpeedCmPerSecond);
	}

	EverydayActivityTimer -= DeltaTime;
	if (bEverydayRoaming)
	{
		const float AcceptanceRadius = FMath::Max(10.f, EverydayDestinationAcceptanceRadiusCm);
		if (FVector::DistSquared2D(GetActorLocation(), EverydayDestination) <= FMath::Square(AcceptanceRadius)
			|| EverydayActivityTimer <= 0.f || AI->GetMoveStatus() != EPathFollowingStatus::Moving)
		{
			BeginEverydayIdle();
		}
		return;
	}

	// 쉬는 동안 고개를 천천히 움직인다.
	EverydayLookElapsed += DeltaTime;
	FRotator Rotation = GetActorRotation();
	Rotation.Yaw = EverydayLookAnchorYaw + FMath::Sin(EverydayLookElapsed * 0.9f) * 18.f;
	SetActorRotation(Rotation);
	if (EverydayActivityTimer <= 0.f) ChooseNextEverydayActivity();
}

void AYUFSEvacuationNPC::ChooseNextEverydayActivity()
{
	const float MinIdle = FMath::Max(0.1f, FMath::Min(EverydayMinIdleSeconds, EverydayMaxIdleSeconds));
	const float MaxIdle = FMath::Max(MinIdle, FMath::Max(EverydayMinIdleSeconds, EverydayMaxIdleSeconds));
	if (EverydayRandomStream.FRand() > FMath::Clamp(EverydayRoamChance, 0.f, 1.f))
	{
		EverydayActivityTimer = EverydayRandomStream.FRandRange(MinIdle, MaxIdle);
		EverydayLookAnchorYaw = GetActorRotation().Yaw;
		EverydayLookElapsed = 0.f;
		return;
	}

	UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(GetWorld());
	AAIController* AI = Cast<AAIController>(GetController());
	if (!NavSystem || !AI)
	{
		BeginEverydayIdle();
		return;
	}

	FNavLocation Candidate;
	bool bFoundDestination = false;
	for (int32 Attempt = 0; Attempt < 4; ++Attempt)
	{
		if (NavSystem->GetRandomReachablePointInRadius(
			EverydayRoamOrigin, FMath::Max(100.f, EverydayRoamRadiusCm), Candidate)
			&& FVector::DistSquared2D(GetActorLocation(), Candidate.Location) > FMath::Square(150.f))
		{
			bFoundDestination = true;
			break;
		}
	}
	if (!bFoundDestination)
	{
		BeginEverydayIdle();
		return;
	}

	EverydayDestination = Candidate.Location;
	const EPathFollowingRequestResult::Type Result = AI->MoveToLocation(
		EverydayDestination, FMath::Max(10.f, EverydayDestinationAcceptanceRadiusCm));
	if (Result == EPathFollowingRequestResult::Failed)
	{
		BeginEverydayIdle();
		return;
	}
	bEverydayRoaming = true;
	EverydayActivityTimer = FMath::Max(1.f, EverydayMaxRoamSeconds);
	CurrentNavTarget = EverydayDestination;
}

void AYUFSEvacuationNPC::BeginEverydayIdle()
{
	if (AAIController* AI = Cast<AAIController>(GetController())) AI->StopMovement();
	if (UCharacterMovementComponent* Mv = GetCharacterMovement()) Mv->StopMovementImmediately();
	bEverydayRoaming = false;
	EverydayDestination = FVector::ZeroVector;
	CurrentNavTarget = FVector::ZeroVector;
	EverydayLookAnchorYaw = GetActorRotation().Yaw;
	EverydayLookElapsed = 0.f;
	const float MinIdle = FMath::Max(0.1f, FMath::Min(EverydayMinIdleSeconds, EverydayMaxIdleSeconds));
	const float MaxIdle = FMath::Max(MinIdle, FMath::Max(EverydayMinIdleSeconds, EverydayMaxIdleSeconds));
	EverydayActivityTimer = EverydayRandomStream.FRandRange(MinIdle, MaxIdle);
}

void AYUFSEvacuationNPC::StopEverydayBehavior()
{
	if (!bEverydayBehaviorActive && !bEverydayRoaming) return;
	if (AAIController* AI = Cast<AAIController>(GetController())) AI->StopMovement();
	if (UCharacterMovementComponent* Mv = GetCharacterMovement()) Mv->StopMovementImmediately();
	bEverydayBehaviorActive = false;
	bEverydayRoaming = false;
	EverydayDestination = FVector::ZeroVector;
	EverydayActivityTimer = 0.f;
	CurrentNavTarget = FVector::ZeroVector;
}

void AYUFSEvacuationNPC::UpdateStuckDetection(float DeltaTime)
{
	UCharacterMovementComponent* Mv = GetCharacterMovement();
	if (!Mv || !Navigator) return;

	const FVector Pos = GetActorLocation();
	const float MaxSpeed = Mv->MaxWalkSpeed;
	if (!Navigator->IsFollowingPath() || IsInteractionHoldingPosition() || LocalMovement->IsDeliberatelyWaiting() || MaxSpeed < KINDA_SMALL_NUMBER)
	{
		StuckTimer = 0.f;
		LastMovementSampleLocation = Pos;
		bHasMovementSample = true;
		return;
	}

	// Detect lack of progress even when collision has reduced velocity to zero.
	const float Moved = bHasMovementSample ? FVector::Dist2D(Pos, LastMovementSampleLocation) : 0.f;
	const float Expected = MaxSpeed * DeltaTime;
	const bool bSlow = Expected > KINDA_SMALL_NUMBER && Moved < Expected * 0.1f;
	if (bSlow)
	{
		StuckTimer += DeltaTime;
		if (StuckTimer > 2.5f)
		{
			const FVector Dir=(Navigator->GetSteeringTarget(Pos)-Pos).GetSafeNormal2D();
			if (!LocalMovement->TryRecovery(Dir,GetCurrentSimFrame()))
				Navigator->ReplanPath(GetCurrentSimFrame(), EYUFSRepathReason::Stuck);
			StuckTimer = 0.f;
		}
	}
	else StuckTimer = 0.f;
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
	Out.bHazardSampleAvailable = PerceptionComp->HasHazardSample();
	Out.bSuppressionAllowedByBehavior = AllowsOptionalInteractions();
	Out.HeatInSight             = PerceptionComp->GetHeatInSight();
	Out.HeatInSightNormalized   = Out.HeatInSight;
	Out.NearbyHeatNormalized    = PerceptionComp->GetNearbyHeat();
	Out.NearbyHeat              = PerceptionComp->GetNearbyHeat();
	Out.bHeardPeerWarning       = SocialComp->HasPeerWarning() || PeerGuidanceSecondsRemaining > 0.f;
	Out.IndividualRoutePreference = BehaviorSM->GetRoutePreference();
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
	// Explicit, locally observed assistance requests complement JJW's physical-distress cue.
	if (TeamIntegrationComp) Out.bNearbyNPCNeedsHelp |= TeamIntegrationComp->GetInteractionOpportunities().bAssistPersonKnown;

	const FVector Pos   = GetActorLocation();
	const int32 Frame   = GetCurrentSimFrame();
	const FVector NExit = ChooseKnownExit();
	const FVector FExit = LevelDataMgr->GetFamiliarExit(SpawnLocation);
	const bool bFoundSafeExit = !NExit.IsZero();

	Out.DistToNearestExit    = bFoundSafeExit ? FVector::Dist(Pos, NExit) : 100000.f;
	Out.DistToFamiliarExit   = FVector::Dist(Pos, FExit);
	Out.DirToNearestExit     = bFoundSafeExit ? (NExit - Pos).GetSafeNormal() : FVector::ZeroVector;
	Out.SimTimeNormalized    = FMath::Clamp(static_cast<float>(Frame) / 8000.f, 0.f, 1.f);
	Out.bNearestExitSmokeFree= bFoundSafeExit && Navigator->GetPerceivedHazardSnapshot(Frame).Sample(NExit+FVector(0,0,120)).Smoke < 0.35f;
}

EYUFSTerminalReason AYUFSEvacuationNPC::GetCurrentTerminalReason() const
{
	if (BehaviorSM && BehaviorSM->IsIncapacitated())
		return EYUFSTerminalReason::Incapacitated;

	if (LevelDataMgr && BehaviorSM)
	{
		const EYUFSBehaviorState State = BehaviorSM->GetCurrentState();
		if (State != EYUFSBehaviorState::Evacuating && State != EYUFSBehaviorState::Crawling)
			return EYUFSTerminalReason::None;

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
	if (SuppressionComp) SuppressionComp->Cancel(false);
	if (EnvironmentInteraction) EnvironmentInteraction->Cancel();
	ResetRetreatKnowledge();
	if (Navigator) Navigator->ResetObservedHazards();
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
	if (SuppressionComp) SuppressionComp->Cancel(false);
	if (EnvironmentInteraction) EnvironmentInteraction->Cancel();
	ResetRetreatKnowledge();
	if (Navigator) Navigator->ResetObservedHazards();

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
			Mv->SetAvoidanceEnabled(true);
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
	if (SuppressionComp) SuppressionComp->ResetForEpisode();
	ResetRetreatKnowledge();
	if (Navigator) { Navigator->ClearPath(); Navigator->ResetObservedHazards(); }
	if (LocalMovement) LocalMovement->Reset();
	if (EnvironmentInteraction) EnvironmentInteraction->Cancel();
	if (TerminalReason == EYUFSTerminalReason::ReachedExit || TerminalReason == EYUFSTerminalReason::Incapacitated)
	{
		// Hiding the actor does not stop CharacterMovement's component tick/RVO updates.
		// Remove resolved residents from avoidance so they cannot crowd an invisible exit.
		ConsumeMovementInputVector();
		GetCharacterMovement()->StopMovementImmediately();
		GetCharacterMovement()->SetAvoidanceEnabled(false);
		GetCharacterMovement()->DisableMovement();
	}
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
	MLPolicy.SetDataCollectionMode(bDataCollectionMode);
	ActionHoldTimer += DeltaTime;
	PolicyTickAccumulator += DeltaTime;
	const bool bInteractionForbidden = bOptionalInteractionSelected && !AllowsOptionalInteractions();
	if (PolicyTickAccumulator >= PolicyTickInterval || bInteractionForbidden
		|| Observation.CurrentState != LastPolicyBehaviorState)
	{
		PolicyTickAccumulator = 0.f;
		const EYUFSAction RawProposal = MLPolicy.SelectAction(Observation);
		EYUFSAction NewAction = ConstrainActionForIntent(RawProposal);
		FYUFSBehaviorDecision Candidate;
		bool bSelectInteraction = false;
		if (bEnableEvidenceDecisionModel && bEnableHumanCognitionModel && AllowsOptionalInteractions()
			&& HumanBehaviorSelector && HumanCognitionComp && TeamIntegrationComp
			&& (!EnvironmentInteraction || !EnvironmentInteraction->IsOperatingDoor()
				|| bOptionalInteractionSelected))
		{
			const auto& Opportunities = TeamIntegrationComp->GetInteractionOpportunities();
			const auto& Decision = HumanBehaviorSelector->ResolveDecision(NewAction, GetCurrentIntent(), Observation,
				HumanCognitionComp->GetCognitiveState(), HumanCognitionComp->GetTraits(), Opportunities, false, DeterministicRng);
			if (Decision.Behavior == EYUFSHighLevelBehavior::AttemptSuppression)
			{
				Candidate = Decision; NewAction = EYUFSAction::Idle; bSelectInteraction = true;
			}
			else if (Opportunities.bAssistPersonKnown && (RawProposal == EYUFSAction::HelpOther
				|| (bOptionalInteractionSelected && ActiveInteractionDecision.Behavior == EYUFSHighLevelBehavior::AssistOther
					&& EnvironmentInteraction && EnvironmentInteraction->IsActive())))
			{
				Candidate.Behavior = EYUFSHighLevelBehavior::AssistOther;
				Candidate.LegacyAction = EYUFSAction::HelpOther;
				Candidate.Reason = TEXT("JJWPolicyObservedAssistance");
				NewAction = EYUFSAction::HelpOther; bSelectInteraction = true;
			}
		}
		if (Observation.CurrentState == EYUFSBehaviorState::Milling) ++MillingActionCount;
		const bool bStateChanged = Observation.CurrentState != LastPolicyBehaviorState;
		const bool bInteractionChanged = bSelectInteraction != bOptionalInteractionSelected
			|| (bSelectInteraction && Candidate.Behavior != ActiveInteractionDecision.Behavior);
		if (NewAction == CurrentAction || bStateChanged || bInteractionChanged || bInteractionForbidden
			|| ActionHoldTimer >= MinActionHoldDuration)
		{
			bOptionalInteractionSelected = bSelectInteraction;
			ActiveInteractionDecision = Candidate;
			if (NewAction != CurrentAction)
			{
				ActionHoldTimer = 0.f;
				CurrentAction = NewAction;
				OnActionChanged(NewAction);
			}
		}
		LastPolicyBehaviorState = Observation.CurrentState;
	}
	PublishTeamDirectives(Observation);
	ExecuteCurrentAction(DeltaTime);
}

EYUFSAction AYUFSEvacuationNPC::ConstrainActionForIntent(EYUFSAction ProposedAction) const
{
	if (!BehaviorSM) return EYUFSAction::Idle;
	if (BehaviorSM->IsIncapacitated()) return EYUFSAction::Cough;
	if (!BehaviorSM->HasCommittedToEvacuation())
		return BehaviorSM->GetCurrentState() == EYUFSBehaviorState::Normal ? EYUFSAction::Idle : EYUFSAction::SeekInformation;
	if (BehaviorSM->GetCurrentState() == EYUFSBehaviorState::Preparing) return EYUFSAction::GatherBelongings;
	if (BehaviorSM->GetCurrentState() == EYUFSBehaviorState::Evacuating || BehaviorSM->IsCrawling())
	{
		if (!IsNavigationAction(ProposedAction) || ProposedAction == EYUFSAction::HelpOther || BehaviorSM->IsCrawling())
			return EYUFSAction::EvacuateToNearestExit;
	}
	return ProposedAction;
}

void AYUFSEvacuationNPC::OnActionChanged(EYUFSAction NewAction)
{
	if (NewAction != EYUFSAction::EvacuateToNearestExit && NewAction != EYUFSAction::EvacuateToFamiliarExit
		&& NewAction != EYUFSAction::FollowCrowd)
	{
		bHasPreferredRetreat = false; PreferredRetreatPath.Reset();
	}
	LookAnchorYaw = GetActorRotation().Yaw;
	LookElapsed   = 0.f;

	if (!bUseExternalNavigationDriver && !IsNavigationAction(NewAction))
	{
		if (Navigator) Navigator->ClearPath();
		if (LocalMovement) LocalMovement->Reset();
		if (UCharacterMovementComponent* Mv = GetCharacterMovement())
			Mv->StopMovementImmediately();
		CurrentNavTarget = FVector::ZeroVector;
	}
	else if (!bUseExternalNavigationDriver)
	{
		if (UCharacterMovementComponent* Mv = GetCharacterMovement())
			if (Mv->MaxWalkSpeed < 1.f) Mv->MaxWalkSpeed = GetDesiredWalkingSpeed();

		const FVector Target = ResolveNavigationTarget(NewAction);
		if (!Target.IsZero() && Navigator)
		{
			if (Navigator->GetNavigationStatus() != EYUFSNavigationStatus::Idle
				&& FVector::DistSquared(Target,CurrentNavTarget)<FMath::Square(100.f)) return;
			CurrentNavTarget = Target;
			Navigator->RequestPathAsync(Target, GetCurrentSimFrame());
		}
	}

	if (!bUseExternalMotionDriver && !bActionAnimationPreviewActive && ActionAnimationComp)
	{
		const EYUFSBehaviorState State = BehaviorSM
			? BehaviorSM->GetCurrentState()
			: EYUFSBehaviorState::Normal;
		ActionAnimationComp->ApplyAction(NewAction, State);
	}
}

void AYUFSEvacuationNPC::UpdateActionAnimation(bool bForce)
{
	const bool bLiveInteraction = !bTimelinePlaybackMode
		&& (!SimulationController || SimulationController->IsNPCSimulationEnabled());
	if (bLiveInteraction && EnvironmentInteraction
		&& (EnvironmentInteraction->IsActive() || EnvironmentInteraction->IsReceivingContactAssistance())) return;
	if (SuppressionComp && (SuppressionComp->IsActive() || SuppressionComp->IsVisualPresentationActive())) return;
	if (bUseExternalMotionDriver || !ActionAnimationComp)
	{
		return;
	}

	// Reuse the compatible walking binding for presentation without changing the policy action.
	const EYUFSAction DisplayAction = bEverydayBehaviorActive && !bActionAnimationPreviewActive
		? (GetVelocity().Size2D() > 5.f ? EYUFSAction::HelpOther : EYUFSAction::Idle)
		: GetDisplayedAction();
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
	TArray<UYUFSNPCDebugComponent*> DebugComponents;
	GetComponents<UYUFSNPCDebugComponent>(DebugComponents);
	for (UYUFSNPCDebugComponent* Component : DebugComponents)
	{
		if (!Component)
		{
			continue;
		}
		Component->SetTemporarilySuppressed(bSuppressed);
		// The close-up interaction showcase must never inherit the regular NPC
		// overlay. Suppress every inherited/Blueprint component instance instead
		// of assuming the native pointer is the only debug renderer.
		Component->SetComponentTickEnabled(!bSuppressed);
	}
}

FString AYUFSEvacuationNPC::GetCurrentActionAnimationName() const
{
	return ActionAnimationComp ? ActionAnimationComp->GetActiveAnimationName() : TEXT("None");
}

void AYUFSEvacuationNPC::ExecuteCurrentAction(float DeltaTime)
{
	bInteractionHoldingPosition = false;
	// Safety is evaluated even while a door is a sub-action of suppression.
	if (SuppressionComp)
	{
		if (SuppressionComp->ReassessSafety(GetCurrentSimFrame())) return;
		SuppressionComp->UpdatePresentation();
	}
	// Only suppress locomotion during contact. The normal Tick has already
	// updated perception, risk and intent, and pause/replay guards still apply.
	if (EnvironmentInteraction && EnvironmentInteraction->IsReceivingContactAssistance())
	{
		auto* Helper=EnvironmentInteraction->GetAssistingNPC();
		if (!Helper || (BehaviorSM && (BehaviorSM->IsIncapacitated() || BehaviorSM->IsCrawling()))
			|| (PerceptionComp && BehaviorSM && BehaviorSM->Config && (PerceptionComp->GetSmokeDensity() > BehaviorSM->Config->SmokeAwarenessThreshold * BehaviorSM->Config->EmergencyOverrideMultiplier
				|| FMath::Max(PerceptionComp->GetTemperature(), PerceptionComp->GetNearbyHeat()) >= BehaviorSM->Config->EmergencyHeatThreshold))
			|| bReceivedLiveAnnouncement || bReceivedStaffGuidance)
		{
			// A recipient must remain able to break contact when its own situation changes.
			if (Helper && Helper->EnvironmentInteraction) Helper->EnvironmentInteraction->Cancel();
		}
		else
		{
			bInteractionHoldingPosition = true;
			if (LocalMovement) LocalMovement->Reset();
			GetCharacterMovement()->StopMovementImmediately();
			if (!bUseExternalMotionDriver && Helper)
			{
				const FRotator Facing(0,(Helper->GetActorLocation()-GetActorLocation()).Rotation().Yaw,0);
				SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(),Facing,DeltaTime,180.f));
				if (ActionAnimationComp) ActionAnimationComp->ApplyAction(EYUFSAction::AlertNearbyOccupants,EYUFSBehaviorState::Normal);
			}
			return;
		}
	}
	if (EnvironmentInteraction && EnvironmentInteraction->Execute(DeltaTime, GetCurrentSimFrame()))
	{
		bInteractionHoldingPosition = true;
		if (LocalMovement) LocalMovement->Reset();
		return;
	}
	if (EnvironmentInteraction && EnvironmentInteraction->NeedsMovement())
	{
		UpdateNavigationMovement(EnvironmentInteraction->GetTarget(), DeltaTime, 180.f);
		return;
	}
	if (bInteractionPreviewControlled) return;
	if (SuppressionComp && SuppressionComp->Execute(DeltaTime, GetCurrentSimFrame()))
	{
		bInteractionHoldingPosition = true;
		if (LocalMovement) LocalMovement->Reset();
		return;
	}
	if (SuppressionComp && SuppressionComp->IsActive())
	{
		UpdateNavigationMovement(SuppressionComp->GetMovementTarget(), DeltaTime, 180.f);
		return;
	}
	if (!BehaviorSM || BehaviorSM->IsIncapacitated()) return;

	switch (CurrentAction)
	{
	case EYUFSAction::SeekInformation:
	case EYUFSAction::AlertNearbyOccupants:
		if (!bUseExternalMotionDriver && BehaviorSM->Config)
		{
			LookElapsed += DeltaTime;
			const float Osc = FMath::Sin(LookElapsed * 2.f * PI * BehaviorSM->Config->LookAroundFrequencyHz * BehaviorSM->GetSpeedMultiplier());
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
		UpdateNavigationMovement(ResolveNavigationTarget(CurrentAction), DeltaTime);
		break;

	default:
		// Idle / WaitForInfo / GatherBelongings — 이동 없음
		break;
	}
}

bool AYUFSEvacuationNPC::TryGetNearestKnownExit(FVector& OutExit) const
{
	OutExit = FVector::ZeroVector;
	RememberFailedExit();
	if (TryGetPreferredRetreat(OutExit)) return true;
	if (!LevelDataMgr || !Navigator) return false;
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	float Best = FLT_MAX;
	for (const FVector& Exit : LevelDataMgr->GetKnownExitLocations())
	{
		const float* RetryAt = FailedExitUntil.Find(Exit);
		if (RetryAt && *RetryAt > Now) continue;
		if (Navigator->IsKnownLocationDangerous(Exit + FVector(0,0,120))) continue;
		const float Distance = FVector::DistSquared(Exit, GetActorLocation());
		if (Distance < Best) { Best = Distance; OutExit = Exit; }
	}
	return Best < FLT_MAX;
}

void AYUFSEvacuationNPC::ResetRetreatKnowledge()
{
	bHasPreferredRetreat = false;
	PreferredRetreatPath.Reset(); PreferredRetreatExit = FVector::ZeroVector;
	PreferredRetreatExpiresAt = 0.f; FailedExitUntil.Reset();
	LastRecordedNavigationFailure = MAX_uint32;
}

void AYUFSEvacuationNPC::RememberFailedExit() const
{
	if (!Navigator || !LevelDataMgr) return;
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	for (auto It = FailedExitUntil.CreateIterator(); It; ++It) if (It.Value() <= Now) It.RemoveCurrent();
	if (Navigator->GetNavigationStatus() != EYUFSNavigationStatus::Failed
		|| LastRecordedNavigationFailure == Navigator->GetRequestGeneration()) return;
	LastRecordedNavigationFailure = Navigator->GetRequestGeneration();
	const FVector FailedGoal = Navigator->GetRequestedDestination();
	for (const FVector& Exit : LevelDataMgr->GetKnownExitLocations())
	{
		if (!Exit.Equals(FailedGoal, 100.f)) continue;
		if (FailedExitUntil.Num() >= 8 && !FailedExitUntil.Contains(Exit))
		{
			FVector Oldest = FVector::ZeroVector; float Earliest = FLT_MAX;
			for (const auto& Pair : FailedExitUntil) if (Pair.Value < Earliest) { Earliest = Pair.Value; Oldest = Pair.Key; }
			FailedExitUntil.Remove(Oldest);
		}
		FailedExitUntil.Add(Exit, Now + 10.f);
		break;
	}
}

bool AYUFSEvacuationNPC::TryGetPreferredRetreat(FVector& OutExit) const
{
	if (!bHasPreferredRetreat || !Navigator) return false;
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const FVector ActiveGoal = Navigator->GetRequestedDestination();
	const bool Failed = Navigator->GetNavigationStatus() == EYUFSNavigationStatus::Failed
		&& ActiveGoal.Equals(PreferredRetreatExit, 100.f);
	const bool Replaced = !ActiveGoal.IsZero() && !ActiveGoal.Equals(PreferredRetreatExit, 100.f);
	bool ExternalFailed = false;
	if (bUseExternalNavigationDriver && TeamIntegrationComp)
	{
		const auto& Directive = TeamIntegrationComp->GetNavigationDirective();
		const auto& Feedback = TeamIntegrationComp->GetNavigationFeedback();
		ExternalFailed = Feedback.RequestRevision == Directive.Revision
			&& Directive.DestinationHint.Equals(PreferredRetreatExit, 100.f)
			&& (Feedback.Status == EYUFSTeamRequestStatus::Failed || Feedback.Status == EYUFSTeamRequestStatus::Blocked);
	}
	if (Now > PreferredRetreatExpiresAt || Failed || Replaced || ExternalFailed
		|| PreferredRetreatPath.Num() < 2 || Navigator->IsKnownPathDangerous(PreferredRetreatPath))
	{
		bHasPreferredRetreat = false; PreferredRetreatPath.Reset();
		return false;
	}
	OutExit = PreferredRetreatExit;
	return true;
}

void AYUFSEvacuationNPC::ResumeEvacuationAfterSuppression(bool bRetreatReachable,
	const FVector& RetreatExit, const TArray<FVector>& RetreatPath)
{
	if (!IntentComp || !BehaviorSM || BehaviorSM->IsIncapacitated()) return;
	if (EnvironmentInteraction) EnvironmentInteraction->Cancel();
	bHasPreferredRetreat = false; PreferredRetreatPath.Reset();
	bRetreatReachable = bRetreatReachable && Navigator && !RetreatExit.ContainsNaN()
		&& RetreatPath.Num() >= 2 && RetreatPath.Num() <= 4096;
	if (bRetreatReachable)
	{
		for (const FVector& Point : RetreatPath) if (Point.ContainsNaN()) { bRetreatReachable = false; break; }
		bRetreatReachable = bRetreatReachable && RetreatPath.Last().Equals(RetreatExit, 150.f)
			&& !Navigator->IsKnownPathDangerous(RetreatPath);
	}
	if (bRetreatReachable)
	{
		PreferredRetreatExit = RetreatExit; PreferredRetreatPath = RetreatPath;
		PreferredRetreatExpiresAt = (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f) + 60.f;
		bHasPreferredRetreat = true;
		FailedExitUntil.Remove(RetreatExit);
		Navigator->ClearPath(); // Any previous approach must not invalidate the handed-off exit preference.
	}
	if (TeamIntegrationComp)
	{
		auto Snapshot = TeamIntegrationComp->GetInteractionOpportunities();
		Snapshot.bDoorActionRequired = false; Snapshot.DoorStableId = NAME_None;
		++Snapshot.KnowledgeRevision;
		TeamIntegrationComp->SubmitInteractionOpportunities(Snapshot);
	}
	IntentComp->ApplyAuthoritativeIntent(GetCurrentIntent());
	bOptionalInteractionSelected = false;
	ActiveInteractionDecision = FYUFSBehaviorDecision{};
	if (LocalMovement) LocalMovement->Reset();
	const EYUFSAction Next = ConstrainActionForIntent(EYUFSAction::EvacuateToNearestExit);
	CurrentAction = Next;
	ActionHoldTimer = MinActionHoldDuration;
	PolicyTickAccumulator = PolicyTickInterval;
	OnActionChanged(Next);
	FYUFSNPCObservation Observation;
	BuildObservation(Observation);
	if (HumanBehaviorSelector) HumanBehaviorSelector->RequestReselection(TEXT("SuppressionFinished"));
	PublishTeamDirectives(Observation);
}

FVector AYUFSEvacuationNPC::ResolveNavigationTarget(EYUFSAction Action) const
{
	if (!LevelDataMgr) return FVector::ZeroVector;
	const FVector Pos   = GetActorLocation();
	const int32   Frame = GetCurrentSimFrame();
	FVector SafeExit = FVector::ZeroVector;
	const bool bFoundSafeExit = TryGetNearestKnownExit(SafeExit);
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	auto RecentlyFailed = [this, Now](const FVector& Exit)
	{
		const float* Until = FailedExitUntil.Find(Exit);
		return Until && *Until > Now;
	};

	switch (Action)
	{
	case EYUFSAction::EvacuateToNearestExit:
		if (bReceivedStaffGuidance && !StaffGuidedExitLocation.IsZero()
			&& !RecentlyFailed(StaffGuidedExitLocation)
			&& Navigator && !Navigator->IsKnownLocationDangerous(StaffGuidedExitLocation + FVector(0,0,120)))
			return StaffGuidedExitLocation;
		return ChooseKnownExit();

	case EYUFSAction::EvacuateToFamiliarExit:
		{
			const FVector Familiar=LevelDataMgr->GetFamiliarExit(SpawnLocation);
			const auto Risk=Navigator->GetPerceivedHazardSnapshot(Frame).Sample(Familiar+FVector(0,0,120));
			return Risk.Smoke>=0.35f || Risk.Heat>=0.35f || Navigator->GetLastFailure()==EYUFSNavigationFailure::UnsafePath ? ChooseKnownExit() : Familiar;
		}

	case EYUFSAction::FollowCrowd:
	{
		FVector Preferred;
		if (TryGetPreferredRetreat(Preferred)) return Preferred;
		if (SocialComp)
		{
			const FVector ObservedDestination = SocialComp->GetObservedEvacuationDestination();
			if (!ObservedDestination.IsZero() && Navigator
				&& !RecentlyFailed(ObservedDestination)
				&& !Navigator->IsKnownLocationDangerous(ObservedDestination + FVector(0,0,120))) return ObservedDestination;
		}
		return ChooseKnownExit();
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

FVector AYUFSEvacuationNPC::ChooseKnownExit() const
{
	if (!LevelDataMgr || !Navigator) return FVector::ZeroVector;
	FVector Preferred;
	if (TryGetPreferredRetreat(Preferred)) return Preferred;
	const auto Snapshot=Navigator->GetPerceivedHazardSnapshot(GetCurrentSimFrame());
	FVector Best=FVector::ZeroVector; float Score=FLT_MAX;
	for (const FVector& Exit:LevelDataMgr->GetExitLocations())
	{
		const auto Risk=Snapshot.Sample(Exit+FVector(0,0,120));
		float Cost=FVector::Dist(GetActorLocation(),Exit)*(1.f+20.f*Risk.Smoke+40.f*Risk.Heat);
		if (Navigator->GetLastFailure()==EYUFSNavigationFailure::UnsafePath && FVector::DistSquared(Exit,CurrentNavTarget)<FMath::Square(100.f)) Cost+=100000.f;
		if (Cost<Score) { Score=Cost; Best=Exit; }
	}
	return Best;
}



void AYUFSEvacuationNPC::ProcessTeamFeedback()
{
	if (!TeamIntegrationComp
		|| TeamIntegrationComp->GetFeedbackGeneration() == LastTeamFeedbackGeneration)
	{
		return;
	}

	LastTeamFeedbackGeneration = TeamIntegrationComp->GetFeedbackGeneration();
	auto IsTerminalFeedback = [](const FYUFSTeamRequestFeedback& Feedback)
	{
		return Feedback.Status == EYUFSTeamRequestStatus::Completed
			|| Feedback.Status == EYUFSTeamRequestStatus::Failed
			|| Feedback.Status == EYUFSTeamRequestStatus::Blocked
			|| Feedback.Status == EYUFSTeamRequestStatus::Cancelled;
	};

	FName Trigger = NAME_None;
	const FYUFSTeamRequestFeedback& InteractionFeedback = TeamIntegrationComp->GetInteractionFeedback();
	const FYUFSTeamRequestFeedback& NavigationFeedback = TeamIntegrationComp->GetNavigationFeedback();
	if (InteractionFeedback.RequestRevision == TeamIntegrationComp->GetInteractionDirective().Revision
		&& IsTerminalFeedback(InteractionFeedback))
	{
		Trigger = InteractionFeedback.Reason.IsNone()
			? TEXT("InteractionFeedback")
			: InteractionFeedback.Reason;
	}
	else if (NavigationFeedback.RequestRevision == TeamIntegrationComp->GetNavigationDirective().Revision
		&& (NavigationFeedback.Status == EYUFSTeamRequestStatus::Failed
		|| NavigationFeedback.Status == EYUFSTeamRequestStatus::Blocked))
	{
		Trigger = NavigationFeedback.Reason.IsNone()
			? TEXT("NavigationBlocked")
			: NavigationFeedback.Reason;
	}

	if (!Trigger.IsNone())
	{
		if (HumanBehaviorSelector)
		{
			HumanBehaviorSelector->RequestReselection(Trigger);
		}
		if (IntentComp)
		{
			IntentComp->RequestReappraisal(Trigger);
		}
	}
}

void AYUFSEvacuationNPC::PublishTeamDirectives(const FYUFSNPCObservation& Observation)
{
	if (!TeamIntegrationComp)
	{
		return;
	}

	FYUFSBehaviorDecision FallbackDecision;
	const FYUFSBehaviorDecision* Decision = bOptionalInteractionSelected
		? &ActiveInteractionDecision : &FallbackDecision;
	if (Decision->Behavior == EYUFSHighLevelBehavior::None)
	{
		FallbackDecision.LegacyAction = CurrentAction;
		FallbackDecision.Reason = TEXT("JJWAuthoritativeAction");
		switch (CurrentAction)
		{
		case EYUFSAction::EvacuateToFamiliarExit: FallbackDecision.Behavior = EYUFSHighLevelBehavior::EvacuateFamiliar; break;
		case EYUFSAction::EvacuateToNearestExit: FallbackDecision.Behavior = EYUFSHighLevelBehavior::EvacuateNearest; break;
		case EYUFSAction::FollowCrowd: FallbackDecision.Behavior = EYUFSHighLevelBehavior::FollowCrowd; break;
		case EYUFSAction::HelpOther: FallbackDecision.Behavior = EYUFSHighLevelBehavior::AssistOther; break;
		case EYUFSAction::GatherBelongings: FallbackDecision.Behavior = EYUFSHighLevelBehavior::RetrieveBelongings; break;
		case EYUFSAction::SeekInformation: FallbackDecision.Behavior = EYUFSHighLevelBehavior::SeekInformation; break;
		case EYUFSAction::Film: FallbackDecision.Behavior = EYUFSHighLevelBehavior::ObserveOrRecord; break;
		default: FallbackDecision.Behavior = EYUFSHighLevelBehavior::WaitObserve; break;
		}
		Decision = &FallbackDecision;
	}

	const FYUFSCognitiveState DefaultCognition;
	const FYUFSCognitiveState& Cognition = HumanCognitionComp
		? HumanCognitionComp->GetCognitiveState()
		: DefaultCognition;
	const FVector DestinationHint = IsNavigationAction(CurrentAction)
		? ResolveNavigationTarget(CurrentAction)
		: FVector::ZeroVector;
	TeamIntegrationComp->PublishDecision(
		StableNPCId,
		*Decision,
		GetCurrentIntent(),
		BehaviorSM ? BehaviorSM->GetCurrentState() : Observation.CurrentState,
		DestinationHint,
		bHasSafeExit,
		Cognition);
}

bool AYUFSEvacuationNPC::RollSocialProbability(float Probability)
{
	return DeterministicRng.IsInitialized()
		? DeterministicRng.Roll(EYUFSRngStream::Social, Probability)
		: Probability >= 0.5f;
}

void AYUFSEvacuationNPC::ApplyDistributedSpawnLocation(const FVector& NewLocation)
{
	// Placement is an episode setup operation, never runtime collision recovery.
	if (SimulationController && SimulationController->IsNPCSimulationEnabled()) return;
	if (SuppressionComp) SuppressionComp->ResetForEpisode();
	if (EnvironmentInteraction) EnvironmentInteraction->Cancel();
	ResetRetreatKnowledge();
	if (Navigator) Navigator->ResetObservedHazards();
	SetActorLocation(NewLocation, false, nullptr, ETeleportType::TeleportPhysics);
	SpawnLocation = NewLocation;
	LastMovementSampleLocation = NewLocation;
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
		DeterministicRng.GetDrawCount(EYUFSRngStream::TaskChoice),
		DeterministicRng.GetDrawCount(EYUFSRngStream::Route),
		DeterministicRng.GetDrawCount(EYUFSRngStream::Social),
		DeterministicRng.GetDrawCount(EYUFSRngStream::InteractionError),
		DeterministicRng.GetDrawCount(EYUFSRngStream::Traits));
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
		DeterministicRng.GetDrawCount(EYUFSRngStream::TaskChoice),
		DeterministicRng.GetDrawCount(EYUFSRngStream::Route),
		DeterministicRng.GetDrawCount(EYUFSRngStream::Social),
		DeterministicRng.GetDrawCount(EYUFSRngStream::InteractionError),
		DeterministicRng.GetDrawCount(EYUFSRngStream::Traits));
}

FString AYUFSEvacuationNPC::GetScenarioHash() const
{
	const FString MapName = GetWorld() ? GetWorld()->GetMapName() : TEXT("NoWorld");
	const FString Canonical = FString::Printf(TEXT("%s|seed=%d"), *MapName, ScenarioSeed);
	return FString::Printf(TEXT("crc32:%08x"), FCrc::StrCrc32(*Canonical));
}
EYUFSIntent AYUFSEvacuationNPC::GetCurrentIntent() const
{
	if (!BehaviorSM) return EYUFSIntent::Observe;
	if (BehaviorSM->IsIncapacitated()) return EYUFSIntent::Incapacitated;
	if (BehaviorSM->GetCurrentState() == EYUFSBehaviorState::Preparing) return EYUFSIntent::Prepare;
	return BehaviorSM->HasCommittedToEvacuation() ? EYUFSIntent::CommitEvac : EYUFSIntent::Observe;
}

float AYUFSEvacuationNPC::GetDesiredWalkingSpeed() const
{
	if (BehaviorSM && BehaviorSM->IsIncapacitated()) return 0.f;
	float Speed = BaseWalkSpeed * (BehaviorSM ? BehaviorSM->GetSpeedMultiplier() : 1.f)
		* (SocialComp ? SocialComp->GetGroupSpeedMultiplier() : 1.f);
	if (BehaviorSM && BehaviorSM->IsCrawling() && BehaviorSM->Config)
		Speed = FMath::Min(Speed, BehaviorSM->Config->CrawlSpeed);
	return Speed;
}

bool AYUFSEvacuationNPC::AllowsOptionalInteractions() const
{
	if (!BehaviorSM || !BehaviorSM->Config || !PerceptionComp
		|| !BehaviorSM->HasCommittedToEvacuation() || BehaviorSM->IsIncapacitated() || BehaviorSM->IsCrawling())
		return false;
	const EYUFSBehaviorState State = BehaviorSM->GetCurrentState();
	if (State != EYUFSBehaviorState::Evacuating && State != EYUFSBehaviorState::Helping) return false;
	const UYUFSBehaviorConfig* Config = BehaviorSM->Config;
	// Identical emergency thresholds to the authoritative JJW decision, including custom DataAssets.
	return PerceptionComp->GetSmokeDensity() <= Config->SmokeAwarenessThreshold * Config->EmergencyOverrideMultiplier
		&& FMath::Max(PerceptionComp->GetTemperature(), PerceptionComp->GetNearbyHeat()) < Config->EmergencyHeatThreshold
		&& !bReceivedLiveAnnouncement && !bReceivedStaffGuidance;
}

void AYUFSEvacuationNPC::ReceivePeerGuidance()
{
	// Information, not forced commitment or teleportation. JJW evaluates this cue next Tick.
	PeerGuidanceSecondsRemaining = 5.f;
	if (HumanBehaviorSelector) HumanBehaviorSelector->RequestReselection(TEXT("ReceivedNearbyGuidance"));
}

void AYUFSEvacuationNPC::UpdateEvidenceDecisionModel(float DeltaTime, FYUFSNPCObservation& Observation)
{
	if (!IntentComp || !BehaviorSM) return;
	IntentComp->ApplyAuthoritativeIntent(GetCurrentIntent());
	if (bEnableEvidenceDecisionModel && bEnableHumanCognitionModel && HumanCognitionComp)
	{
		HumanCognitionComp->UpdateCognition(DeltaTime, Observation);
		const auto& Cognition = HumanCognitionComp->GetCognitiveState();
		const auto& Traits = HumanCognitionComp->GetTraits();
		if (BeliefComp) BeliefComp->SetCognitiveContext(Cognition.NormalcyBias, Traits.SocialConformity, Traits.AuthorityTrust);
		if (HumanCognitionComp->DidEvidenceChange() && HumanBehaviorSelector)
			HumanBehaviorSelector->RequestReselection(Cognition.LastEvidenceTrigger);
	}
	if (BeliefComp) BeliefComp->UpdateBelief(Observation);
	LastSafeExit = ChooseKnownExit();
	bHasSafeExit = !LastSafeExit.IsZero(); // Known goal, not proof of hazard-free travel.
	Observation.bSuppressionAllowedByBehavior = AllowsOptionalInteractions();
	if (IntentComp->DidIntentChange())
	{
		if (HumanBehaviorSelector) HumanBehaviorSelector->RequestReselection(TEXT("JJWStateChanged"));
		TraceIntentTransition();
	}
}

void AYUFSEvacuationNPC::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (SuppressionComp) SuppressionComp->Cancel(false);
	if (EnvironmentInteraction) EnvironmentInteraction->Cancel();
	if (Navigator) Navigator->ClearPath();
	if (IsValid(SimulationController)) SimulationController->UnregisterNPC(this);
	Super::EndPlay(EndPlayReason);
}

bool AYUFSEvacuationNPC::IsInteractionHoldingPosition() const
{
	return bInteractionHoldingPosition;
}

void AYUFSEvacuationNPC::UpdateNavigationMovement(const FVector& Target, float DeltaTime, float SpeedCap)
{
	if (bUseExternalNavigationDriver || !Navigator || !LocalMovement) return;
	if (Target.IsZero() || Target.ContainsNaN())
	{
		Navigator->ClearPath(); LocalMovement->Reset(); CurrentNavTarget = FVector::ZeroVector;
		GetCharacterMovement()->StopMovementImmediately();
		return;
	}
	if (SpeedCap > 0.f) GetCharacterMovement()->MaxWalkSpeed = FMath::Min(GetDesiredWalkingSpeed(), SpeedCap);
	if (LocalMovement->TickRecovery(DeltaTime, GetCurrentSimFrame())) return;
	if (Navigator->GetLastFailure() == EYUFSNavigationFailure::StartOffNavMesh
		&& LocalMovement->TryRecovery((Target - GetActorLocation()).GetSafeNormal2D(), GetCurrentSimFrame())) return;
	// JJW requests cancel the previous async generation. Never restart an empty pending/failed path every frame.
	if (Navigator->GetNavigationStatus() == EYUFSNavigationStatus::Idle
		|| FVector::DistSquared(Target, CurrentNavTarget) > FMath::Square(100.f))
	{
		CurrentNavTarget = Target;
		Navigator->RequestPathAsync(Target, GetCurrentSimFrame());
	}
	else if (Navigator->ShouldRetryPath())
		Navigator->ReplanPath(GetCurrentSimFrame(), EYUFSRepathReason::Retry);
	else Navigator->CheckAndReroute(GetCurrentSimFrame());
	if (Navigator->IsFollowingPath()) DriveMovementToward(Target, DeltaTime);
}
