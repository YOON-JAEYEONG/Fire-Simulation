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
	LastMovementSampleLocation = SpawnLocation;
	LastPositionCheckLocation  = SpawnLocation;
	bHasMovementSample = true;

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
		if (UCharacterMovementComponent* Mv = GetCharacterMovement())
		{
			Mv->StopMovementImmediately();
			Mv->MaxWalkSpeed = 0.f;
		}
		return;
	}

	if (bInteractionPreviewControlled)
	{
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
				if (!bUseExternalNavigationDriver && Navigator)
				{
					// Navigator stores a floor-projected destination, while this hint can
					// be at capsule height. Height alone must not restart the same path.
					if (Navigator->GetCurrentPathPoints().IsEmpty()
						|| !Navigator->GetRequestedDestination().Equals(InteractionPreviewDestination, 5.f))
						Navigator->RequestPathAsync(InteractionPreviewDestination,GetCurrentSimFrame());
					GetCharacterMovement()->MaxWalkSpeed = 150.f;
					DriveMovementToward(InteractionPreviewDestination,35.f);
				}
				if (!bUseExternalMotionDriver && ActionAnimationComp)
					ActionAnimationComp->ApplyAction(GetVelocity().Size2D()>5.f?EYUFSAction::HelpOther:EYUFSAction::Idle,EYUFSBehaviorState::Normal);
			}
			else if (!bUseExternalMotionDriver && ActionAnimationComp)
				ActionAnimationComp->ApplyAction(EYUFSAction::WaitForInfo,EYUFSBehaviorState::Normal);
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
	if (SuppressionComp) SuppressionComp->Observe(DeltaTime, CurrentFrame, CurrentObs);
	if (EnvironmentInteraction) EnvironmentInteraction->Observe(DeltaTime);
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

	// ── 근거 기반 Belief → Intent 갱신 및 V1 상태 투영 ────────────────
	UpdateEvidenceDecisionModel(DeltaTime, CurrentObs);

	// ── MLP 정책 추론 및 액션 실행 ───────────────────────────────────────
	TickPolicy(DeltaTime, CurrentObs);

	if (bEnableEvidenceDecisionModel && ActionTaskComp && BeliefComp && IntentComp)
	{
		if (bEnableHumanCognitionModel && HumanBehaviorSelector)
		{
			const FYUFSBehaviorDecision& Decision = HumanBehaviorSelector->GetCurrentDecision();
			ActionTaskComp->UpdateDesiredTask(
				DeltaTime,
				Decision.DesiredTask,
				Decision.Revision,
				IntentComp->GetCurrentIntent(),
				BeliefComp->HasImmediateLifeRisk(),
				BeliefComp->HasVerifiedOfficialInstruction(),
				DeterministicRng);
		}
		else
		{
			ActionTaskComp->UpdateTask(
				DeltaTime,
				CurrentAction,
				IntentComp->GetCurrentIntent(),
				BeliefComp->HasImmediateLifeRisk(),
				BeliefComp->HasVerifiedOfficialInstruction(),
				DeterministicRng);
		}

		EYUFSActionTask FromTask = EYUFSActionTask::None;
		EYUFSActionTask ToTask = EYUFSActionTask::None;
		EYUFSTaskCancelReason Reason = EYUFSTaskCancelReason::None;
		while (ActionTaskComp->ConsumeTaskEvent(FromTask, ToTask, Reason))
		{
			const EYUFSActionTask EventTask = ToTask != EYUFSActionTask::None ? ToTask : FromTask;
			TraceTaskEvent(EventTask, Reason, ToTask != EYUFSActionTask::None ? TEXT("TaskStarted") : TEXT("TaskEnded"));
			if (Reason == EYUFSTaskCancelReason::Completed)
			{
				if (HumanBehaviorSelector)
				{
					HumanBehaviorSelector->NotifyTaskFinished(EventTask);
				}
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
	PublishTeamDirectives(CurrentObs);
	CurrentObs.MillingActionCount = MillingActionCount;

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

void AYUFSEvacuationNPC::DriveMovementToward(FVector Target, float AcceptanceRadius)
{
	if (!Navigator || Target.IsZero()) return;

	// 일시정지 → 재개 시 MaxWalkSpeed 가 0 으로 남아있는 경우 복원
	// (Crawling 속도 제한은 NPC Tick 에서 별도로 cap 하므로 여기선 무조건 양수만 보장)
	if (UCharacterMovementComponent* Mv = GetCharacterMovement())
	{
		if (Mv->MaxWalkSpeed < 1.f)
			Mv->MaxWalkSpeed = 300.f;
	}

	Navigator->UpdateWaypoint(GetActorLocation(), AcceptanceRadius);
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
 auto* Movement = GetCharacterMovement();
 if (!Movement || !Navigator || bUseExternalNavigationDriver) return;
 const FVector Position = GetActorLocation();
 const bool IntentionalStop = (EnvironmentInteraction && (EnvironmentInteraction->IsActive()
  && !EnvironmentInteraction->NeedsMovement() || EnvironmentInteraction->IsReceivingContactAssistance()))
  || (SuppressionComp && SuppressionComp->IsSpraying());
 if (!Navigator->IsFollowingPath() || IntentionalStop || Movement->MaxWalkSpeed < 1.f
  || (BehaviorSM && BehaviorSM->IsIncapacitated()))
 {
  StuckTimer = PositionStuckTimer = 0.f;
  LastMovementSampleLocation = LastPositionCheckLocation = Position;
  bHasMovementSample = true;
  return;
 }
 const float Progress = bHasMovementSample ? FVector::Dist2D(Position, LastMovementSampleLocation) : 0.f;
 const float Expected = Movement->MaxWalkSpeed * FMath::Max(0.f, DeltaTime);
 // Actual displacement, not velocity: a capsule blocked at zero velocity is still stuck.
 const bool NoProgress = Expected > UE_SMALL_NUMBER && Progress < Expected * 0.10f;
 StuckTimer = NoProgress ? StuckTimer + FMath::Max(0.f, DeltaTime) : 0.f;
 if (StuckTimer >= 2.5f)
 {
  Navigator->ReportMovementBlocked();
  if (TeamIntegrationComp)
  {
   FYUFSTeamRequestFeedback Feedback;
   Feedback.RequestRevision = TeamIntegrationComp->GetNavigationDirective().Revision;
   Feedback.Status = EYUFSTeamRequestStatus::Blocked;
   Feedback.Reason = TEXT("PhysicalPassageBlocked");
   Feedback.ResolvedLocation = Position;
   TeamIntegrationComp->SubmitNavigationFeedback(Feedback);
  }
  UE_LOG(LogTemp, Display, TEXT("[YUFS][Traffic] %s blocked at %s; physical collision retained, bounded replan only"),
   *GetName(), *Position.ToCompactString());
  StuckTimer = 0.f;
 }
 LastMovementSampleLocation = LastPositionCheckLocation = Position;
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
	Out.HeatInSightNormalized   = PerceptionComp->GetHeatInSightNormalized();
	Out.NearbyHeatNormalized    = PerceptionComp->GetNearbyHeatNormalized();
	Out.bHazardSampleAvailable  = PerceptionComp->HasHazardSample();
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
	const bool bFoundSafeExit = TryGetNearestKnownExit(NExit);
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

	if (bEnableHumanCognitionModel && HumanCognitionComp)
	{
		HumanCognitionComp->UpdateCognition(DeltaTime, Observation);
		const FYUFSCognitiveState& Cognition = HumanCognitionComp->GetCognitiveState();
		const FYUFSHumanTraits& Traits = HumanCognitionComp->GetTraits();
		BehaviorSM->ApplyCognitiveRisk(Cognition.PerceivedRisk);
		Observation.StressLevel = Cognition.Stress;
		BeliefComp->SetCognitiveContext(
			Cognition.NormalcyBias,
			Traits.SocialConformity,
			Traits.AuthorityTrust);
		if (HumanCognitionComp->DidEvidenceChange() && HumanBehaviorSelector)
		{
			HumanBehaviorSelector->RequestReselection(Cognition.LastEvidenceTrigger);
		}
	}
	BeliefComp->UpdateBelief(Observation);
	bHasSafeExit = TryGetNearestKnownExit(LastSafeExit);
	if (!bHasSafeExit)
	{
		LastSafeExit = FVector::ZeroVector;
	}

	const int64 EvidenceRevision = bEnableHumanCognitionModel && HumanCognitionComp
		? HumanCognitionComp->GetCognitiveState().EvidenceRevision
		: 0;
	IntentComp->UpdateIntent(
		DeltaTime,
		Observation,
		*BeliefComp,
		bHasSafeExit,
		DeterministicRng,
		EvidenceRevision);
	BehaviorSM->ApplyIntentProjection(IntentComp->GetCurrentIntent());

	// 기존 UI/ONNX V1은 호환 투영된 BehaviorState를 계속 읽는다.
	Observation.CurrentState = BehaviorSM->GetCurrentState();
	Observation.RiskPerception = BehaviorSM->GetRiskPerception();
	Observation.SmokeExposureAccumulated = BehaviorSM->GetSmokeExposure();

	if (IntentComp->DidIntentChange())
	{
		if (HumanCognitionComp)
		{
			HumanCognitionComp->NotifyPlanChanged();
		}
		if (HumanBehaviorSelector)
		{
			HumanBehaviorSelector->RequestReselection(TEXT("IntentChanged"));
		}
		TraceIntentTransition();
	}
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
	const FYUFSBehaviorDecision* Decision = HumanBehaviorSelector
		? &HumanBehaviorSelector->GetCurrentDecision()
		: &FallbackDecision;
	if (Decision->Behavior == EYUFSHighLevelBehavior::None)
	{
		FallbackDecision.LegacyAction = CurrentAction;
		FallbackDecision.Reason = TEXT("LegacyFallback");
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
		IntentComp ? IntentComp->GetCurrentIntent() : EYUFSIntent::Observe,
		BehaviorSM ? BehaviorSM->GetCurrentState() : Observation.CurrentState,
		DestinationHint,
		bHasSafeExit,
		Cognition);
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
	// Placement is an episode setup operation, never runtime collision recovery.
	if (SimulationController && SimulationController->IsNPCSimulationEnabled()) return;
	if (SuppressionComp) SuppressionComp->ResetForEpisode();
	if (EnvironmentInteraction) EnvironmentInteraction->Cancel();
	ResetRetreatKnowledge();
	if (Navigator) Navigator->ResetObservedHazards();
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
	if (EnvironmentInteraction) EnvironmentInteraction->Cancel();
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

		const EYUFSAction ProposedAction = ConstrainActionForIntent(MLPolicy.SelectAction(Observation));
		EYUFSAction NewAction = ProposedAction;
		if (bEnableHumanCognitionModel && HumanBehaviorSelector && HumanCognitionComp && IntentComp)
		{
			const FYUFSInteractionOpportunitySnapshot EmptyOpportunities;
			const FYUFSInteractionOpportunitySnapshot& Opportunities = TeamIntegrationComp
				? TeamIntegrationComp->GetInteractionOpportunities()
				: EmptyOpportunities;
			const FYUFSBehaviorDecision& Decision = HumanBehaviorSelector->ResolveDecision(
				ProposedAction,
				IntentComp->GetCurrentIntent(),
				Observation,
				HumanCognitionComp->GetCognitiveState(),
				HumanCognitionComp->GetTraits(),
				Opportunities,
				HumanCognitionComp->ConsumeFreezeCue(),
				DeterministicRng);
			NewAction = Decision.LegacyAction;
		}

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

	// Publish the chosen interaction before this frame's executor reads the contract.
	PublishTeamDirectives(Observation);
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
		if (UCharacterMovementComponent* Mv = GetCharacterMovement())
			Mv->StopMovementImmediately();
		CurrentNavTarget = FVector::ZeroVector;
	}
	else if (!bUseExternalNavigationDriver)
	{
		if (UCharacterMovementComponent* Mv = GetCharacterMovement())
			if (Mv->MaxWalkSpeed < 1.f) Mv->MaxWalkSpeed = 400.f;

		const FVector Target = ResolveNavigationTarget(NewAction);
		if (!Target.IsZero() && Navigator)
		{
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
	if (SuppressionComp && SuppressionComp->IsActive()) return;
	if (bUseExternalMotionDriver || !ActionAnimationComp)
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
		if (BeliefComp && (BeliefComp->HasImmediateLifeRisk() || BeliefComp->HasVerifiedOfficialInstruction()))
		{
			// A recipient must remain able to break contact when its own situation changes.
			if (Helper && Helper->EnvironmentInteraction) Helper->EnvironmentInteraction->Cancel();
		}
		else
		{
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
	if (EnvironmentInteraction && EnvironmentInteraction->Execute(DeltaTime, GetCurrentSimFrame())) return;
	if (EnvironmentInteraction && EnvironmentInteraction->NeedsMovement())
	{
		const FVector Target = EnvironmentInteraction->GetTarget();
		if (!bUseExternalNavigationDriver && Navigator)
		{
			Navigator->CheckAndReroute(GetCurrentSimFrame());
			if (Navigator->GetCurrentPathPoints().IsEmpty()
				|| FVector::DistSquared(Target, CurrentNavTarget) > FMath::Square(100.f))
			{
				CurrentNavTarget = Target;
				Navigator->RequestPathAsync(Target, GetCurrentSimFrame());
			}
			if (!Navigator->GetCurrentPathPoints().IsEmpty())
			{
				GetCharacterMovement()->MaxWalkSpeed = 180.f;
				DriveMovementToward(Target, 100.f);
			}
		}
		return;
	}
	if (bInteractionPreviewControlled) return;
	if (SuppressionComp && SuppressionComp->Execute(DeltaTime, GetCurrentSimFrame())) return;
	if (SuppressionComp && SuppressionComp->IsActive())
	{
		const FVector Target = SuppressionComp->GetMovementTarget();
		if (!bUseExternalNavigationDriver && Navigator)
		{
			Navigator->CheckAndReroute(GetCurrentSimFrame());
			if (Navigator->GetCurrentPathPoints().IsEmpty()
				|| FVector::DistSquared(Target, CurrentNavTarget) > FMath::Square(100.f))
			{
				CurrentNavTarget = Target;
				Navigator->RequestPathAsync(Target, GetCurrentSimFrame());
			}
			if (!Navigator->GetCurrentPathPoints().IsEmpty())
			{
				GetCharacterMovement()->MaxWalkSpeed = 180.f;
				DriveMovementToward(Target, 20.f);
			}
		}
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
		if (!bUseExternalNavigationDriver && Navigator)
		{
			const FVector Target = ResolveNavigationTarget(CurrentAction);
			if (Target.IsZero()) break;

			Navigator->CheckAndReroute(GetCurrentSimFrame());

			if (Navigator->GetCurrentPathPoints().IsEmpty()
				|| FVector::Dist(Target, CurrentNavTarget) > 100.f)
			{
				CurrentNavTarget = Target;
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
	IntentComp->ResumeEvacuationAfterInteraction(bRetreatReachable);
	BehaviorSM->ApplyIntentProjection(IntentComp->GetCurrentIntent());
	const EYUFSAction Next = bRetreatReachable ? EYUFSAction::EvacuateToNearestExit : EYUFSAction::WaitForInfo;
	CurrentAction = Next;
	ActionHoldTimer = MinActionHoldDuration;
	PolicyTickAccumulator = PolicyTickInterval;
	OnActionChanged(Next);
	FYUFSNPCObservation Observation;
	BuildObservation(Observation);
	if (HumanBehaviorSelector && HumanCognitionComp && TeamIntegrationComp)
		HumanBehaviorSelector->ResolveDecision(Next, IntentComp->GetCurrentIntent(), Observation,
			HumanCognitionComp->GetCognitiveState(), HumanCognitionComp->GetTraits(),
			TeamIntegrationComp->GetInteractionOpportunities(), false, DeterministicRng);
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
		return bFoundSafeExit ? SafeExit : FVector::ZeroVector;

	case EYUFSAction::EvacuateToFamiliarExit:
	{
		FVector Preferred;
		if (TryGetPreferredRetreat(Preferred)) return Preferred;
		const FVector FamiliarExit = LevelDataMgr->GetFamiliarExit(SpawnLocation);
		return Navigator && !RecentlyFailed(FamiliarExit) && !Navigator->IsKnownLocationDangerous(FamiliarExit + FVector(0,0,120))
			? FamiliarExit
			: (bFoundSafeExit ? SafeExit : FVector::ZeroVector);
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
		return bFoundSafeExit ? SafeExit : FVector::ZeroVector;
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
