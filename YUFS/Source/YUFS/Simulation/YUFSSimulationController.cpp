#include "Simulation/YUFSSimulationController.h"

#include "Blueprint/UserWidget.h"
#include "Simulation/YUFSGameInstance.h"
#include "Simulation/YUFSTimelineRecorder.h"
#include "Communication/YUFSEmergencyCommSystem.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Fire/YUFSBinaryManager.h"
#include "Fire/YUFSHeterogeneousVolume.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Level/YUFSLevelDataManager.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "NavigationSystem.h"

AYUFSSimulationController::AYUFSSimulationController()
{
	PrimaryActorTick.bCanEverTick = true;

	// 타임라인 기록/관찰 로직은 별도 컴포넌트에 분리합니다.
	// SimulationController는 Phase 전환과 외부 API만 담당합니다.
	TimelineRecorder = CreateDefaultSubobject<UYUFSTimelineRecorder>(TEXT("YUFSTimelineRecorder"));
}

void AYUFSSimulationController::BeginPlay()
{
	Super::BeginPlay();

	// 씬에서 필요한 액터들 캐싱
	for (TActorIterator<AYUFSBinaryManager> It(GetWorld()); It; ++It)
	{
		BinaryManager = *It;
		break;
	}
	for (TActorIterator<AYUFSEmergencyCommSystem> It(GetWorld()); It; ++It)
	{
		CommSystem = *It;
		break;
	}
	for (TActorIterator<AYUFSHeterogeneousVolume> It(GetWorld()); It; ++It)
	{
		HeterogeneousVolume = *It;
		break;
	}
	for (TActorIterator<AYUFSLevelDataManager> It(GetWorld()); It; ++It)
	{
		CachedLDM = *It;
		break;
	}

	// 씬에 이미 배치된 NPC들 자동 수집
	for (TActorIterator<AYUFSEvacuationNPC> It(GetWorld()); It; ++It)
	{
		RegisterNPC(*It);
	}
	InitialNPCCount = RegisteredNPCs.Num();
	ScheduleNPCDistribution();
	if (BinaryManager && HeterogeneousVolume)
	{
		BinaryManager->SetHeterogeneousVolume(HeterogeneousVolume);
	}
	if (TimelineRecorder)
	{
		TimelineRecorder->Initialize(this, HeterogeneousVolume);
	}

	SpawnHUD();

	// 레벨 리로드 후 배치 실험 복원 — GameInstance에 저장된 회차 상태를 읽어옴
	if (UYUFSGameInstance* GI = GetGameInstance<UYUFSGameInstance>())
	{
		if (GI->bHasPendingBatchRun)
		{
			CurrentRunIndex = GI->PendingRunIndex;
			TotalRunCount   = GI->PendingTotalRuns;
			AllRunResults   = GI->AccumulatedResults;
			GI->ClearBatchState();

			UE_LOG(LogTemp, Log, TEXT("[YUFS] Batch resume: Run %d/%d"), CurrentRunIndex, TotalRunCount);
			SetPhase(ESimPhase::FireStartDelay);
		}
	}
}

void AYUFSSimulationController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 관찰 모드는 일반 시뮬레이션 Pause와 독립적으로 동작합니다.
	// 재생/일시정지는 TimelineRecorder 내부의 bReviewPlaying으로 제어합니다.
	if (CurrentPhase == ESimPhase::TimelineReview)
	{
		if (TimelineRecorder)
		{
			TimelineRecorder->TickReview(DeltaTime, RegisteredNPCs);
		}
		return;
	}

	if (bIsPaused) return;
	if (CurrentPhase == ESimPhase::WaitingToStart || CurrentPhase == ESimPhase::Completed) return;

	ElapsedSimTime += DeltaTime;

	if (CurrentPhase == ESimPhase::FireStartDelay)
	{
		FirePhaseTimer += DeltaTime;
		if (FirePhaseTimer >= FireStartDelaySeconds)
		{
			SetPhase(ESimPhase::FireActive);
		}
	}
	else if (CurrentPhase == ESimPhase::FireActive)
	{
		TickFireActivePhase(DeltaTime);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 외부 제어 API
// ─────────────────────────────────────────────────────────────────────────────

void AYUFSSimulationController::StartSimulation()
{
	if (CurrentPhase != ESimPhase::WaitingToStart) return;

	CurrentRunIndex = 1;
	bIsPaused = false;
	ElapsedSimTime = 0.f;
	FirePhaseTimer = 0.f;
	bAlarmFired = false;
	bPreRecordedMsgFired = false;
	bLiveAnnouncementFired = false;
	bStaffGuidanceFired = false;
	InitialNPCCount = RegisteredNPCs.Num();
	LiveEvacuatedCount = 0;
	LiveIncapacitatedCount = 0;
	TotalEvacuationTime = 0.f;
	AllRunResults.Empty();
	ResolvedNPCs.Empty();

	SetPhase(ESimPhase::FireStartDelay);

	UE_LOG(LogTemp, Log, TEXT("[YUFS] Simulation Run %d/%d started. Fire in %.0f seconds."),
		CurrentRunIndex, TotalRunCount, FireStartDelaySeconds);
}

void AYUFSSimulationController::PauseSimulation()
{
	if (CurrentPhase == ESimPhase::TimelineReview)
	{
		PauseTimeline();
		return;
	}

	bIsPaused = true;

	if (HeterogeneousVolume)
	{
		HeterogeneousVolume->PauseFire();
	}

	for (AYUFSEvacuationNPC* NPC : RegisteredNPCs)
	{
		if (!IsValid(NPC)) continue;

		if (UCharacterMovementComponent* MoveComp = NPC->GetCharacterMovement())
		{
			MoveComp->StopMovementImmediately();
			MoveComp->DisableMovement();
		}

		NPC->SetActorTickEnabled(false);
	}

	// 이 줄은 제거
	// UGameplayStatics::SetGlobalTimeDilation(GetWorld(), 0.0001f);
}

void AYUFSSimulationController::ResumeSimulation()
{
	if (CurrentPhase == ESimPhase::TimelineReview)
	{
		PlayTimeline();
		return;
	}

	bIsPaused = false;

	if (HeterogeneousVolume && CurrentPhase == ESimPhase::FireActive)
	{
		HeterogeneousVolume->ResumeFire();
	}

	for (AYUFSEvacuationNPC* NPC : RegisteredNPCs)
	{
		if (!IsValid(NPC)) continue;

		NPC->SetActorTickEnabled(true);

		if (UCharacterMovementComponent* MoveComp = NPC->GetCharacterMovement())
		{
			MoveComp->SetMovementMode(MOVE_Walking);
		}
	}

	// 이것도 필요 없음
	// UGameplayStatics::SetGlobalTimeDilation(GetWorld(), 1.f);
}

void AYUFSSimulationController::StopAndResetSimulation()
{
	UE_LOG(LogTemp, Log, TEXT("[YUFS] Simulation STOPPED. Reloading level..."));

	// TimeDilation을 먼저 정상화한 뒤 레벨을 리로드해야 다음 실행 시 정상 속도로 시작됨
	UGameplayStatics::SetGlobalTimeDilation(GetWorld(), 1.f);

	// 레벨 리로드: 파괴된 NPC, 화재 볼륨 상태 등 모든 것을 완전히 초기화
	UGameplayStatics::OpenLevel(GetWorld(), *GetWorld()->GetName());
}

// ─────────────────────────────────────────────────────────────────────────────
// 내부 로직
// ─────────────────────────────────────────────────────────────────────────────

void AYUFSSimulationController::SetPhase(ESimPhase NewPhase)
{
	if (CurrentPhase == NewPhase) return;
	CurrentPhase = NewPhase;
	OnPhaseChanged.Broadcast(NewPhase);

	switch (NewPhase)
	{
	case ESimPhase::FireStartDelay:
		// 화재 전 준비 단계: HeterogeneousVolume을 리셋해두고 정지 상태 유지
		if (HeterogeneousVolume) HeterogeneousVolume->ResetFire();
		UE_LOG(LogTemp, Log, TEXT("[YUFS] Phase: FireStartDelay — Volume reset, waiting %.0f sec."),
			FireStartDelaySeconds);
		break;

	case ESimPhase::FireActive:
		// 화재 시작: HeterogeneousVolume 재생 개시
		if (HeterogeneousVolume) HeterogeneousVolume->StartFire();
		if (bEnableTimelineRecording && TimelineRecorder)
		{
			TimelineRecorder->BeginRecording(TimelineRecordEndFireSeconds, TimelineRecordIntervalSeconds);
		}
		UE_LOG(LogTemp, Warning, TEXT("[YUFS] Phase: FireActive === 🔥 Fire STARTED ==="));
		break;

	case ESimPhase::TimelineReview:
		// 기록 종료 후 관찰 모드: 화재와 NPC AI를 멈추고 스냅샷만 적용합니다.
		if (HeterogeneousVolume) HeterogeneousVolume->PauseFire();
		UE_LOG(LogTemp, Warning, TEXT("[YUFS] Phase: TimelineReview — time travel / observation mode."));
		break;

	case ESimPhase::Completed:
		// 종료: 볼륨 일시정지
		if (HeterogeneousVolume) HeterogeneousVolume->PauseFire();
		UE_LOG(LogTemp, Warning, TEXT("[YUFS] Phase: Completed — Volume paused."));
		break;

	case ESimPhase::WaitingToStart:
		// 완전 리셋
		if (HeterogeneousVolume) HeterogeneousVolume->ResetFire();
		break;

	default:
		break;
	}
}

void AYUFSSimulationController::TickFireActivePhase(float DeltaTime)
{
	const float FireElapsed = ElapsedSimTime - FireStartDelaySeconds;

	// 알람 발령
	if (!bAlarmFired && FireElapsed >= AlarmTriggerOffsetSeconds)
	{
		if (CommSystem)
		{
			CommSystem->ActivateAlarm();
			bAlarmFired = true;
			UE_LOG(LogTemp, Warning, TEXT("[YUFS] === ALARM ACTIVATED ==="));
		}
	}

	// 사전 녹음 방송
	if (!bPreRecordedMsgFired && PreRecordedMsgOffsetSeconds >= 0.f
		&& FireElapsed >= PreRecordedMsgOffsetSeconds)
	{
		if (CommSystem)
		{
			CommSystem->BroadcastPreRecordedMessage();
			bPreRecordedMsgFired = true;
			UE_LOG(LogTemp, Warning, TEXT("[YUFS] === PRE-RECORDED MESSAGE BROADCAST ==="));
		}
	}

	// 실시간 안내 방송
	if (!bLiveAnnouncementFired && LiveAnnouncementOffsetSeconds >= 0.f
		&& FireElapsed >= LiveAnnouncementOffsetSeconds)
	{
		if (CommSystem)
		{
			CommSystem->BroadcastLiveAnnouncement();
			bLiveAnnouncementFired = true;
			UE_LOG(LogTemp, Warning, TEXT("[YUFS] === LIVE ANNOUNCEMENT BROADCAST ==="));
		}
	}

	// 스태프 직접 안내 — 발령 시점의 가장 안전한 출구로 목적지 자동 결정
	if (!bStaffGuidanceFired && StaffGuidanceOffsetSeconds >= 0.f
		&& FireElapsed >= StaffGuidanceOffsetSeconds)
	{
		if (CommSystem && CachedLDM)
		{
			const int32 CurrentFrame = BinaryManager ? BinaryManager->GetCurrentFrame() : 0;
			const FVector SafeExit = CachedLDM->GetNearestSafeExit(
				CommSystem->GetActorLocation(), true, CurrentFrame);
			CommSystem->DispatchStaffGuidance(SafeExit);
			bStaffGuidanceFired = true;
			UE_LOG(LogTemp, Warning, TEXT("[YUFS] === STAFF GUIDANCE DISPATCHED → %s ==="),
				*SafeExit.ToString());
		}
	}

	UpdateLiveCounts();

	const int32 CurrentFrame = BinaryManager ? BinaryManager->GetCurrentFrame() : 0;
	if (bEnableTimelineRecording && TimelineRecorder)
	{
		TimelineRecorder->TickRecording(
			DeltaTime,
			FireElapsed,
			CurrentFrame,
			RegisteredNPCs,
			LiveEvacuatedCount,
			LiveIncapacitatedCount);

		// 사용자가 지정한 시간까지 기록이 끝나면 종료가 아니라 관찰 모드로 전환합니다.
		if (FireElapsed >= TimelineRecordEndFireSeconds)
		{
			EnterTimelineReviewMode();
			return;
		}
	}
	else
	{
		CheckCompletionCondition();
		if (CurrentPhase == ESimPhase::Completed)
		{
			return;
		}
	}

	// 최대 시뮬레이션 시간 초과 시 강제 종료
	if (ElapsedSimTime - FireStartDelaySeconds >= MaxSimDurationSeconds)
	{
		UE_LOG(LogTemp, Warning, TEXT("[YUFS] Max simulation duration reached. Forcing end."));
		if (bEnableTimelineRecording)
		{
			EnterTimelineReviewMode();
		}
		else
		{
			FinalizeRun();
		}
	}
}

void AYUFSSimulationController::UpdateLiveCounts()
{
	if (!CachedLDM || !BinaryManager) return;

	const int32 CurrentFrame = BinaryManager->GetCurrentFrame();

	for (AYUFSEvacuationNPC* NPC : RegisteredNPCs)
	{
		if (!IsValid(NPC) || ResolvedNPCs.Contains(NPC))
		{
			continue;
		}

		UYUFSBehaviorStateMachine* SM = NPC->GetBehaviorStateMachine();
		if (!SM) continue;

		// 행동불능 카운트: 삭제하지 않고 숨겨야 타임라인에서 과거 시점 복원이 가능합니다.
		if (SM->GetCurrentState() == EYUFSBehaviorState::Incapacitated)
		{
			NPC->NotifyEpisodeFinished(EYUFSTerminalReason::Incapacitated);
			LiveIncapacitatedCount++;
			ResolvedNPCs.Add(NPC);

			NPC->SetActorHiddenInGame(true);
			NPC->SetActorEnableCollision(false);
			NPC->SetActorTickEnabled(false);
			continue;
		}

		// 출구 도달 여부 확인
		const FVector NearestExit = CachedLDM->GetNearestSafeExit(
			NPC->GetActorLocation(), false, CurrentFrame);
		const float DistToExit = FVector::Distance(NPC->GetActorLocation(), NearestExit);

		if (DistToExit < EvacuationSuccessDistanceCm)
		{
			NPC->NotifyEpisodeFinished(EYUFSTerminalReason::ReachedExit);
			LiveEvacuatedCount++;
			TotalEvacuationTime += ElapsedSimTime;
			ResolvedNPCs.Add(NPC);

			UE_LOG(LogTemp, Log, TEXT("[YUFS] NPC '%s' evacuated successfully. Total: %d"),
				*NPC->GetName(), LiveEvacuatedCount);

			// 기존 코드의 Destroy()는 타임라인 복원을 불가능하게 만듭니다.
			// Actor는 유지하고 화면/충돌/Tick만 끕니다.
			NPC->SetActorHiddenInGame(true);
			NPC->SetActorEnableCollision(false);
			NPC->SetActorTickEnabled(false);
		}
	}
}

void AYUFSSimulationController::CheckCompletionCondition()
{
	// RegisteredNPCs를 제거하지 않으므로, 해결된 NPC 수로 종료를 판단합니다.
	const int32 TotalCount = InitialNPCCount > 0 ? InitialNPCCount : RegisteredNPCs.Num();
	if (TotalCount > 0 && ResolvedNPCs.Num() >= TotalCount)
	{
		UE_LOG(LogTemp, Warning, TEXT("[YUFS] All NPCs resolved. Finalizing run %d."), CurrentRunIndex);
		FinalizeRun();
	}
}

void AYUFSSimulationController::FinalizeRun()
{
	if (!RegisteredNPCs.IsEmpty())
	{
		for (AYUFSEvacuationNPC* NPC : RegisteredNPCs)
		{
			if (IsValid(NPC) && !ResolvedNPCs.Contains(NPC))
			{
				NPC->NotifyEpisodeFinished(EYUFSTerminalReason::TimedOut);
				if (UCharacterMovementComponent* MovementComp = NPC->GetCharacterMovement())
				{
					MovementComp->StopMovementImmediately();
					MovementComp->DisableMovement();
				}
				NPC->SetActorTickEnabled(false);
			}
		}
	}

	SetPhase(ESimPhase::Completed);

	// 결과 요약 구성
	FSimRunResult Result;
	Result.RunIndex = CurrentRunIndex;
	Result.TotalNPCCount = FMath::Max(InitialNPCCount, LiveEvacuatedCount + LiveIncapacitatedCount + RegisteredNPCs.Num());
	Result.EvacuatedCount = LiveEvacuatedCount;
	Result.IncapacitatedCount = LiveIncapacitatedCount;
	Result.EvacuationRate = Result.TotalNPCCount > 0
		? (float)Result.EvacuatedCount / (float)Result.TotalNPCCount
		: 0.f;
	Result.SimDurationSeconds = ElapsedSimTime;
	Result.AverageEvacuationTime = Result.EvacuatedCount > 0
		? TotalEvacuationTime / static_cast<float>(Result.EvacuatedCount)
		: 0.f;

	AllRunResults.Add(Result);
	OnRunCompleted.Broadcast(Result);

	UE_LOG(LogTemp, Warning,
		TEXT("[YUFS] Run %d/%d Done | Evacuated: %d/%d (%.1f%%) | Incapacitated: %d | Duration: %.1fs"),
		CurrentRunIndex, TotalRunCount,
		Result.EvacuatedCount, Result.TotalNPCCount,
		Result.EvacuationRate * 100.f,
		Result.IncapacitatedCount,
		Result.SimDurationSeconds);

	// 배치 실험: 다음 회차 진행
	if (CurrentRunIndex < TotalRunCount)
	{
		FTimerHandle NextRunTimer;
		GetWorld()->GetTimerManager().SetTimer(NextRunTimer, this,
			&AYUFSSimulationController::StartNextRun,
			DelayBetweenRunsSeconds, false);
	}
}

void AYUFSSimulationController::StartNextRun()
{
	// 레벨 리로드 전에 다음 회차 상태를 GameInstance에 보존
	// OpenLevel 이후 이 액터는 파괴되므로 멤버 변수에 저장해봐야 소용 없음
	if (UYUFSGameInstance* GI = GetGameInstance<UYUFSGameInstance>())
	{
		GI->SetupNextRun(CurrentRunIndex + 1, TotalRunCount, AllRunResults);
	}

	UGameplayStatics::SetGlobalTimeDilation(GetWorld(), 1.f);
	UGameplayStatics::OpenLevel(GetWorld(), *GetWorld()->GetName());
}


// ─────────────────────────────────────────────────────────────────────────────
// 타임라인 기록/관찰 API
// ─────────────────────────────────────────────────────────────────────────────

void AYUFSSimulationController::StartTimelineRecordingSimulation(float InRecordEndFireSeconds)
{
	TimelineRecordEndFireSeconds = FMath::Max(0.f, InRecordEndFireSeconds);
	bEnableTimelineRecording = true;
	StartSimulation();
}

void AYUFSSimulationController::EnterTimelineReviewMode()
{
	if (CurrentPhase == ESimPhase::TimelineReview)
	{
		return;
	}

	bIsPaused = false;

	if (HeterogeneousVolume)
	{
		HeterogeneousVolume->PauseFire();
	}

	for (AYUFSEvacuationNPC* NPC : RegisteredNPCs)
	{
		if (!IsValid(NPC))
		{
			continue;
		}

		NPC->SetActorTickEnabled(true);
		NPC->SetTimelinePlaybackMode(true);
	}

	if (TimelineRecorder)
	{
		TimelineRecorder->EnterReviewMode(RegisteredNPCs);
	}

	SetPhase(ESimPhase::TimelineReview);
}

void AYUFSSimulationController::SeekTimelineBySeconds(float FireElapsedSeconds)
{
	if (CurrentPhase != ESimPhase::TimelineReview || !TimelineRecorder)
	{
		return;
	}

	TimelineRecorder->SeekToFireTime(FireElapsedSeconds, RegisteredNPCs);
}

void AYUFSSimulationController::SeekTimelineByNormalizedValue(float NormalizedValue)
{
	if (!TimelineRecorder)
	{
		return;
	}

	const float TargetTime = FMath::Clamp(NormalizedValue, 0.f, 1.f) * TimelineRecorder->GetMaxRecordedFireTime();
	SeekTimelineBySeconds(TargetTime);
}

void AYUFSSimulationController::PlayTimeline()
{
	if (CurrentPhase == ESimPhase::TimelineReview && TimelineRecorder)
	{
		TimelineRecorder->PlayReview();
	}
}

void AYUFSSimulationController::PauseTimeline()
{
	if (CurrentPhase == ESimPhase::TimelineReview && TimelineRecorder)
	{
		TimelineRecorder->PauseReview();
	}
}

float AYUFSSimulationController::GetTimelineCurrentTime() const
{
	return TimelineRecorder ? TimelineRecorder->GetCurrentReviewFireTime() : 0.f;
}

float AYUFSSimulationController::GetTimelineMaxTime() const
{
	return TimelineRecorder ? TimelineRecorder->GetMaxRecordedFireTime() : 0.f;
}

float AYUFSSimulationController::GetTimelineProgress01() const
{
	return TimelineRecorder ? TimelineRecorder->GetTimelineProgress01() : 0.f;
}

bool AYUFSSimulationController::IsTimelinePlaying() const
{
	return TimelineRecorder && TimelineRecorder->IsReviewPlaying();
}

void AYUFSSimulationController::RegisterNPC(AYUFSEvacuationNPC* NPC)
{
	if (IsValid(NPC) && !RegisteredNPCs.Contains(NPC))
	{
		RegisteredNPCs.Add(NPC);
		if (CurrentPhase == ESimPhase::WaitingToStart)
		{
			InitialNPCCount = RegisteredNPCs.Num();
			ScheduleNPCDistribution();
		}
	}
}

void AYUFSSimulationController::ScheduleNPCDistribution()
{
	if (!bDistributeOverlappingNPCs || !GetWorld() || CurrentPhase != ESimPhase::WaitingToStart)
	{
		return;
	}

	GetWorldTimerManager().SetTimer(
		NPCDistributionTimerHandle,
		this,
		&AYUFSSimulationController::DistributeRegisteredNPCs,
		FMath::Max(NPCDistributionDelaySeconds, 0.01f),
		false);
}

void AYUFSSimulationController::DistributeRegisteredNPCs()
{
	if (!bDistributeOverlappingNPCs || CurrentPhase != ESimPhase::WaitingToStart)
	{
		return;
	}

	TArray<AYUFSEvacuationNPC*> NPCs;
	for (AYUFSEvacuationNPC* NPC : RegisteredNPCs)
	{
		if (IsValid(NPC) && !NPC->IsHidden())
		{
			NPCs.Add(NPC);
		}
	}

	NPCs.Sort([](const AYUFSEvacuationNPC& Left, const AYUFSEvacuationNPC& Right)
	{
		if (Left.GetStableNPCId() != Right.GetStableNPCId())
		{
			return Left.GetStableNPCId() < Right.GetStableNPCId();
		}
		return Left.GetName() < Right.GetName();
	});

	UNavigationSystemV1* NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (NPCs.Num() < 2)
	{
		return;
	}

	TArray<float> RawIndoorFloorLevels;
	for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
	{
		const UStaticMeshComponent* Component = It->GetStaticMeshComponent();
		if (!Component || Component->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
		{
			continue;
		}

		const FBoxSphereBounds Bounds = Component->Bounds;
		const float TopZ = Bounds.Origin.Z + Bounds.BoxExtent.Z;
		if (Bounds.BoxExtent.X >= IndoorFloorSurfaceMinExtentCm
			&& Bounds.BoxExtent.Y >= IndoorFloorSurfaceMinExtentCm
			&& Bounds.BoxExtent.Z <= IndoorFloorSurfaceMaxHalfThicknessCm
			&& TopZ >= -200.f
			&& TopZ <= 2000.f)
		{
			RawIndoorFloorLevels.Add(TopZ);
		}
	}

	RawIndoorFloorLevels.Sort();
	TArray<float> IndoorFloorLevels;
	TArray<int32> IndoorFloorLevelSampleCounts;
	for (const float SurfaceZ : RawIndoorFloorLevels)
	{
		if (IndoorFloorLevels.IsEmpty()
			|| FMath::Abs(SurfaceZ - IndoorFloorLevels.Last()) > IndoorFloorGroupingToleranceCm)
		{
			IndoorFloorLevels.Add(SurfaceZ);
			IndoorFloorLevelSampleCounts.Add(1);
		}
		else
		{
			const int32 LastIndex = IndoorFloorLevels.Num() - 1;
			const int32 NewSampleCount = IndoorFloorLevelSampleCounts[LastIndex] + 1;
			IndoorFloorLevels[LastIndex] =
				(IndoorFloorLevels[LastIndex] * IndoorFloorLevelSampleCounts[LastIndex] + SurfaceZ)
				/ static_cast<float>(NewSampleCount);
			IndoorFloorLevelSampleCounts[LastIndex] = NewSampleCount;
		}
	}

	if (IndoorFloorLevels.IsEmpty())
	{
		const float CapsuleHalfHeight = NPCs[0]->GetCapsuleComponent()
			? NPCs[0]->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
			: 88.f;
		IndoorFloorLevels.Add(NPCs[0]->GetActorLocation().Z - CapsuleHalfHeight);
	}
	IndoorFloorLevels.SetNum(FMath::Min(
		IndoorFloorLevels.Num(),
		FMath::Max(NPCDistributionTargetFloorCount, 1)));

	FString FloorLevelSummary;
	for (int32 FloorIndex = 0; FloorIndex < IndoorFloorLevels.Num(); ++FloorIndex)
	{
		FloorLevelSummary += FString::Printf(
			TEXT("%s%dF=%.1fcm"),
			FloorIndex > 0 ? TEXT(", ") : TEXT(""),
			FloorIndex + 1,
			IndoorFloorLevels[FloorIndex]);
	}
	UE_LOG(LogTemp, Log, TEXT("[YUFS] Indoor floor levels detected: %s."), *FloorLevelSummary);

	const float ClusterRadiusSq = FMath::Square(FMath::Max(NPCDistributionClusterRadiusCm, 50.f));
	const float Spacing = FMath::Max(NPCDistributionSpacingCm, 80.f);
	const float MinimumSpacingSq = FMath::Square(Spacing * 0.8f);
	const float MaxRadius = FMath::Max(NPCDistributionMaxRadiusCm, Spacing);
	const FVector ProjectionExtent(Spacing, Spacing, 500.f);
	constexpr float GoldenAngleRadians = 2.39996323f;
	const int32 MaxPlacementAttempts = FMath::Max(NPCDistributionMaxPlacementAttempts, 64);
	TArray<bool> Assigned;
	Assigned.Init(false, NPCs.Num());
	int32 TotalMoved = 0;
	int32 NavProjectionSuccessCount = 0;
	int32 FloorResolutionSuccessCount = 0;
	int32 TeleportRejectionCount = 0;
	int32 CeilingValidatedPlacementCount = 0;
	int32 EnclosureFallbackPlacementCount = 0;
	TArray<int32> FloorPlacementCounts;
	FloorPlacementCounts.Init(0, IndoorFloorLevels.Num());

	for (int32 SeedIndex = 0; SeedIndex < NPCs.Num(); ++SeedIndex)
	{
		if (Assigned[SeedIndex])
		{
			continue;
		}

		TArray<int32> ClusterIndices { SeedIndex };
		Assigned[SeedIndex] = true;
		for (int32 QueueIndex = 0; QueueIndex < ClusterIndices.Num(); ++QueueIndex)
		{
			const FVector QueueLocation = NPCs[ClusterIndices[QueueIndex]]->GetActorLocation();
			for (int32 CandidateIndex = 0; CandidateIndex < NPCs.Num(); ++CandidateIndex)
			{
				if (!Assigned[CandidateIndex]
					&& FVector::DistSquared2D(QueueLocation, NPCs[CandidateIndex]->GetActorLocation()) <= ClusterRadiusSq)
				{
					Assigned[CandidateIndex] = true;
					ClusterIndices.Add(CandidateIndex);
				}
			}
		}

		if (ClusterIndices.Num() < 2)
		{
			continue;
		}

		FVector ClusterCenter = FVector::ZeroVector;
		for (const int32 Index : ClusterIndices)
		{
			ClusterCenter += NPCs[Index]->GetActorLocation();
		}
		ClusterCenter /= static_cast<float>(ClusterIndices.Num());

		TArray<FVector> AcceptedLocations;
		for (int32 MemberIndex = 0; MemberIndex < ClusterIndices.Num(); ++MemberIndex)
		{
			AYUFSEvacuationNPC* NPC = NPCs[ClusterIndices[MemberIndex]];
			const int32 TargetFloorIndex = ClusterIndices[MemberIndex] % IndoorFloorLevels.Num();
			const float CapsuleHalfHeight = NPC->GetCapsuleComponent()
				? NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
				: 88.f;
			const float TargetActorZ = IndoorFloorLevels[TargetFloorIndex] + CapsuleHalfHeight + 2.f;
			bool bPlaced = false;
			const int32 PlacementPassCount = bRequireIndoorNPCPlacement && bPreferCeilingCollisionForIndoorPlacement ? 2 : 1;
			for (int32 PlacementPass = 0; PlacementPass < PlacementPassCount && !bPlaced; ++PlacementPass)
			{
				const bool bRequireCeilingThisPass =
					bRequireIndoorNPCPlacement && bPreferCeilingCollisionForIndoorPlacement && PlacementPass == 0;
				for (int32 Attempt = 0; Attempt < MaxPlacementAttempts; ++Attempt)
				{
					// All members scan the same dense deterministic field. Accepted points
					// reject later overlaps, so each member naturally continues outward.
					const int32 SpiralIndex = Attempt;
					const float Radius = FMath::Min(
						Spacing * 0.55f * FMath::Sqrt(static_cast<float>(SpiralIndex)),
						MaxRadius);
					const float Angle = GoldenAngleRadians * static_cast<float>(SpiralIndex);
					const FVector DesiredLocation = FVector(ClusterCenter.X, ClusterCenter.Y, TargetActorZ) + FVector(
						FMath::Cos(Angle) * Radius,
						FMath::Sin(Angle) * Radius,
						0.f);

					FVector CandidateLocation = DesiredLocation;
					FNavLocation ProjectedLocation;
					if (NavSystem && NavSystem->ProjectPointToNavigation(DesiredLocation, ProjectedLocation, ProjectionExtent))
					{
						CandidateLocation.X = ProjectedLocation.Location.X;
						CandidateLocation.Y = ProjectedLocation.Location.Y;
						++NavProjectionSuccessCount;
					}

					if (bRequireIndoorNPCPlacement
						&& !TryResolveIndoorNPCSpawnLocation(CandidateLocation, NPC, bRequireCeilingThisPass, CandidateLocation))
					{
						continue;
					}
					if (bRequireIndoorNPCPlacement)
					{
						++FloorResolutionSuccessCount;
					}
					if (FMath::Abs(CandidateLocation.Z - TargetActorZ) > NPCDistributionMaxFloorDeltaCm)
					{
						continue;
					}

					bool bTooClose = false;
					for (const FVector& AcceptedLocation : AcceptedLocations)
					{
						const bool bSameFloor =
							FMath::Abs(CandidateLocation.Z - AcceptedLocation.Z) <= IndoorFloorGroupingToleranceCm;
						if (bSameFloor
							&& FVector::DistSquared2D(CandidateLocation, AcceptedLocation) < MinimumSpacingSq)
						{
							bTooClose = true;
							break;
						}
					}
					if (bTooClose)
					{
						continue;
					}

					FVector TeleportLocation = CandidateLocation;
					if (!GetWorld()->FindTeleportSpot(NPC, TeleportLocation, NPC->GetActorRotation()))
					{
						++TeleportRejectionCount;
						continue;
					}

					// FindTeleportSpot may move a valid indoor candidate onto the outdoor
					// landscape. Treat its result as untrusted and validate it again.
					if (bRequireIndoorNPCPlacement)
					{
						FVector RevalidatedLocation;
						if (!TryResolveIndoorNPCSpawnLocation(
							TeleportLocation,
							NPC,
							bRequireCeilingThisPass,
							RevalidatedLocation))
						{
							continue;
						}
						TeleportLocation = RevalidatedLocation;
						if (FMath::Abs(TeleportLocation.Z - TargetActorZ) > NPCDistributionMaxFloorDeltaCm)
						{
							continue;
						}
					}

					bTooClose = false;
					for (const FVector& AcceptedLocation : AcceptedLocations)
					{
						const bool bSameFloor =
							FMath::Abs(TeleportLocation.Z - AcceptedLocation.Z) <= IndoorFloorGroupingToleranceCm;
						if (bSameFloor
							&& FVector::DistSquared2D(TeleportLocation, AcceptedLocation) < MinimumSpacingSq)
						{
							bTooClose = true;
							break;
						}
					}
					if (bTooClose)
					{
						continue;
					}

					AcceptedLocations.Add(TeleportLocation);
					++FloorPlacementCounts[TargetFloorIndex];
					if (bRequireCeilingThisPass)
					{
						++CeilingValidatedPlacementCount;
					}
					else
					{
						++EnclosureFallbackPlacementCount;
					}
					if (!NPC->GetActorLocation().Equals(TeleportLocation, 1.f))
					{
						NPC->ApplyDistributedSpawnLocation(TeleportLocation);
						++TotalMoved;
					}
					bPlaced = true;
					break;
				}
			}

			if (!bPlaced)
			{
				AcceptedLocations.Add(NPC->GetActorLocation());
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("[YUFS] No valid indoor spawn slot found for NPC %s on floor %d."),
					*NPC->GetName(),
					TargetFloorIndex + 1);
			}
		}
	}

	FString FloorPlacementSummary;
	for (int32 FloorIndex = 0; FloorIndex < FloorPlacementCounts.Num(); ++FloorIndex)
	{
		FloorPlacementSummary += FString::Printf(
			TEXT("%s%dF=%d"),
			FloorIndex > 0 ? TEXT(", ") : TEXT(""),
			FloorIndex + 1,
			FloorPlacementCounts[FloorIndex]);
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[YUFS] NPC spawn distribution complete: %d/%d moved, floors [%s], spacing %.0f cm, ceiling-validated %d, wall-enclosure fallback %d, nav projections %d, floor hits %d, collision rejections %d."),
		TotalMoved,
		NPCs.Num(),
		*FloorPlacementSummary,
		Spacing,
		CeilingValidatedPlacementCount,
		EnclosureFallbackPlacementCount,
		NavProjectionSuccessCount,
		FloorResolutionSuccessCount,
		TeleportRejectionCount);
}

bool AYUFSSimulationController::TryResolveIndoorNPCSpawnLocation(
	const FVector& DesiredLocation,
	AYUFSEvacuationNPC* NPC,
	bool bRequireCeiling,
	FVector& OutLocation) const
{
	UWorld* World = GetWorld();
	if (!World || !NPC)
	{
		return false;
	}

	FCollisionObjectQueryParams StaticObjectQuery;
	StaticObjectQuery.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(YUFSIndoorNPCPlacement), false);
	QueryParams.AddIgnoredActor(NPC);

	FHitResult FloorHit;
	const bool bHasFloor = World->LineTraceSingleByObjectType(
		FloorHit,
		DesiredLocation + FVector(0.f, 0.f, 100.f),
		DesiredLocation - FVector(0.f, 0.f, 1000.f),
		StaticObjectQuery,
		QueryParams);
	if (!bHasFloor)
	{
		return false;
	}
	if (!Cast<UStaticMeshComponent>(FloorHit.GetComponent()))
	{
		// The large outdoor Landscape surrounds the CAD building and must never
		// become an NPC spawn floor.
		return false;
	}

	const float CapsuleHalfHeight = NPC->GetCapsuleComponent()
		? NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
		: 88.f;
	// Imported NPC actors can be authored well above the visible floor. Snap the
	// capsule center to the actual collision floor so the rendered mesh stays inside
	// the walls in both world space and the perspective overview camera.
	OutLocation = FVector(
		DesiredLocation.X,
		DesiredLocation.Y,
		FloorHit.ImpactPoint.Z + CapsuleHalfHeight + 2.f);

	if (!bRequireCeiling)
	{
		const float EnclosureTraceDistance = FMath::Max(IndoorEnclosureTraceDistanceCm, 500.f);
		const FVector TraceStart = OutLocation + FVector(0.f, 0.f, CapsuleHalfHeight * 0.25f);
		const FVector Directions[] =
		{
			FVector::ForwardVector,
			-FVector::ForwardVector,
			FVector::RightVector,
			-FVector::RightVector
		};

		int32 EnclosedDirectionCount = 0;
		for (const FVector& Direction : Directions)
		{
			FHitResult WallHit;
			if (World->LineTraceSingleByObjectType(
				WallHit,
				TraceStart,
				TraceStart + Direction * EnclosureTraceDistance,
				StaticObjectQuery,
				QueryParams))
			{
				++EnclosedDirectionCount;
			}
		}

		return EnclosedDirectionCount == UE_ARRAY_COUNT(Directions);
	}

	FHitResult CeilingHit;
	return World->LineTraceSingleByObjectType(
		CeilingHit,
		OutLocation + FVector(0.f, 0.f, CapsuleHalfHeight + 20.f),
		OutLocation + FVector(0.f, 0.f, FMath::Max(IndoorCeilingTraceHeightCm, 200.f)),
		StaticObjectQuery,
		QueryParams);
}

float AYUFSSimulationController::GetFireStartCountdown() const
{
	if (CurrentPhase == ESimPhase::FireStartDelay)
	{
		return FMath::Max(0.f, FireStartDelaySeconds - FirePhaseTimer);
	}
	return 0.f;
}

void AYUFSSimulationController::SpawnHUD()
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC || !HUDWidgetClass) return;

	HUDWidgetInstance = CreateWidget<UUserWidget>(PC, HUDWidgetClass);
	if (HUDWidgetInstance)
	{
		HUDWidgetInstance->AddToViewport();
	}
}
