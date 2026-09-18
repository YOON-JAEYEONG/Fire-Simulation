#include "Simulation/YUFSSimulationController.h"

#include "Blueprint/UserWidget.h"
#include "Simulation/YUFSGameInstance.h"
#include "Debug/YUFSInteractionPreview.h"
#include "Simulation/YUFSTimelineRecorder.h"
#include "Communication/YUFSEmergencyCommSystem.h"
#include "EngineUtils.h"
#include "Fire/YUFSBinaryManager.h"
#include "Fire/YUFSHeterogeneousVolume.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Level/YUFSLevelDataManager.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

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
	bWaitForInteractionPreview = FParse::Param(FCommandLine::Get(), TEXT("YUFSBuildingInteractions"))
		&& FParse::Param(FCommandLine::Get(), TEXT("YUFSInteractionPreview"));
	// Old serialized defaults must not reactivate relocation/camera takeover.
	// JJW's palette is the only runtime author of NPC placement.
	bDistributeOverlappingNPCs = false;
	bPreviewAllNPCActionAnimations = false;
	bAutoFocusNPCActionAnimationShowcase = false;

	// 씬에서 필요한 액터들 캐싱
	for (TActorIterator<AYUFSEmergencyCommSystem> It(GetWorld()); It; ++It)
	{
		CommSystem = *It;
		break;
	}
	for (TActorIterator<AYUFSLevelDataManager> It(GetWorld()); It; ++It)
	{
		CachedLDM = *It;
		break;
	}

	// 레벨의 (단일) BinaryManager/HeterogeneousVolume을 찾아 링크하고,
	// FireOptions[0](또는 마지막으로 선택했던 인덱스)를 적용합니다.
	FindFireSceneActors();
	if (FireOptions.IsValidIndex(CurrentFireOptionIndex))
	{
		ApplyFireOption(CurrentFireOptionIndex);
	}
	else if (!FireOptions.IsEmpty())
	{
		ApplyFireOption(0);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[YUFS] FireOptions가 비어 있습니다. SimulationController의 Fire Options 배열에 화재(표시 이름/.bin 경로/머티리얼)를 최소 1개 등록하세요."));
	}

	// 화재 볼륨이 실제 머티리얼을 가지고 있는지 확인합니다.
	for (TActorIterator<AYUFSHeterogeneousVolume> It(GetWorld()); It; ++It)
	{
		const auto* VolumeComponent = It->FindComponentByClass<UHeterogeneousVolumeComponent>();
		if (!VolumeComponent || !VolumeComponent->GetMaterial(0))
		{
			UE_LOG(LogTemp, Warning, TEXT("[YUFS] Fire volume '%s' has no material. Set FireMaterial on the matching FireOptions entry (or assign a material on the HeterogeneousVolume directly) so playback is visible."), *It->GetName());
		}
	}

	// 씬에 이미 배치된 NPC들 자동 수집
	for (TActorIterator<AYUFSEvacuationNPC> It(GetWorld()); It; ++It)
	{
		RegisterNPC(*It);
	}
	InitialNPCCount = RegisteredNPCs.Num();

	// "이 회차 재현" 요청이 있으면 Start 버튼을 누르기 전, 레벨이 열리자마자 NPC 배치를
	// 복원합니다(부족한 NPC는 재스폰). 시드 소비/실제 실행 시작은 여전히 StartSimulation()의
	// InitializeRunSeed()가 담당하므로 여기서는 플래그를 지우지 않습니다.
	if (const UYUFSGameInstance* GI = GetGameInstance<UYUFSGameInstance>())
	{
		if (GI->bHasPendingReplaySeed)
		{
			ApplyReplayNPCTransforms(GI->PendingReplayNPCTransforms, GI->PendingReplayNPCClasses);
			InitialNPCCount = RegisteredNPCs.Num();
		}
	}

	if (BinaryManager && HeterogeneousVolume)
	{
		BinaryManager->SetHeterogeneousVolume(HeterogeneousVolume);
	}
	if (TimelineRecorder)
	{
		TimelineRecorder->Initialize(this, HeterogeneousVolume);
	}

	// 메인 메뉴에서 넘어온 시나리오 설정을 먼저 반영합니다.
	// (배치 회차마다 레벨이 리로드되므로 매 BeginPlay에서 다시 적용)
	ApplyActiveScenario();

	SpawnHUD();

	// 소화기 등 건물 상호작용 소품 배치 — 레벨을 다시 열 때마다(Stop/Reset 포함) 재적용됩니다.
	if (UYUFSGameInstance* GI = GetGameInstance<UYUFSGameInstance>())
	{
		GI->SetupBuildingInteractions(GetWorld());
	}
}

void AYUFSSimulationController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HUDWidgetInstance)
	{
		HUDWidgetInstance->RemoveFromParent();
		HUDWidgetInstance = nullptr;
	}
	Super::EndPlay(EndPlayReason);
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

	if (CurrentPhase == ESimPhase::WaitingToStart)
	{
		return;
	}

	if (bIsPaused) return;
	if (CurrentPhase == ESimPhase::Completed) return;

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
	if (IsWaitingForInteractionPreview())
	{
		bStartRequestedBeforePreviewReady = true;
		UE_LOG(LogTemp, Display, TEXT("[InteractionPreview] Start queued until the explicitly requested interaction fixture is ready. NPC positions remain user-authored."));
		return;
	}
	bStartRequestedBeforePreviewReady = false;

	StopNPCActionAnimationShowcase();

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

	// 결과 화면의 "이 회차 재현" 요청이 있으면 그 시드로, 아니면 새 랜덤 시드로 시작합니다.
	InitializeRunSeed();

	SetPhase(ESimPhase::FireStartDelay);
	// Both HUD implementations enter here. Start exactly one visual sequence.
	for (TActorIterator<AYUFSInteractionPreview> It(GetWorld()); It; ++It)
		It->StartSequence();

	UE_LOG(LogTemp, Log, TEXT("[YUFS] Simulation started. Fire in %.0f seconds."), FireStartDelaySeconds);
}

void AYUFSSimulationController::NotifyInteractionPreviewReady(bool bReady)
{
	bInteractionPreviewReady = bReady;
	UE_LOG(LogTemp, Display, TEXT("[InteractionPreview] Setup %s; queued start=%d."),
		bReady ? TEXT("ready") : TEXT("FAILED"), bStartRequestedBeforePreviewReady);
	if (bReady && bStartRequestedBeforePreviewReady) StartSimulation();
}

void AYUFSSimulationController::PauseSimulation()
{
	if (CurrentPhase == ESimPhase::TimelineReview)
	{
		PauseTimeline();
		return;
	}
	if (CurrentPhase == ESimPhase::WaitingToStart)
	{
		bStartRequestedBeforePreviewReady = false;
		return;
	}

	bIsPaused = true;
	for (TActorIterator<AYUFSInteractionPreview> It(GetWorld()); It; ++It)
		It->SetSimulationPaused(true);

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

	if (!bIsPaused) return;
	bIsPaused = false;
	for (TActorIterator<AYUFSInteractionPreview> It(GetWorld()); It; ++It)
		It->SetSimulationPaused(false);

	if (HeterogeneousVolume && CurrentPhase == ESimPhase::FireActive)
	{
		HeterogeneousVolume->ResumeFire();
	}

	for (AYUFSEvacuationNPC* NPC : RegisteredNPCs)
	{
		if (!IsValid(NPC) || ResolvedNPCs.Contains(NPC)) continue;

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
	StopNPCActionAnimationShowcase();

	// 회차가 실제로 진행 중일 때(화재 대기/진행)만 중단 시점 기준으로 결과를 저장합니다.
	// Completed/TimelineReview는 FinalizeRun()/EnterTimelineReviewMode()에서 이미 저장했으므로
	// 여기서 또 저장하면 같은 회차가 기록에 중복으로 쌓입니다.
	if (CurrentPhase == ESimPhase::FireStartDelay || CurrentPhase == ESimPhase::FireActive)
	{
		StoreRunResult(BuildRunResult());
	}

	// TimeDilation을 먼저 정상화한 뒤 레벨을 리로드해야 다음 실행 시 정상 속도로 시작됨
	UGameplayStatics::SetGlobalTimeDilation(GetWorld(), 1.f);

	// 레벨 리로드: 파괴된 NPC, 화재 볼륨 상태 등 모든 것을 완전히 초기화
	UGameplayStatics::OpenLevel(GetWorld(), *GetWorld()->GetName());
}

void AYUFSSimulationController::SelectFireOption(int32 OptionIndex)
{
	if (CurrentPhase != ESimPhase::WaitingToStart)
	{
		UE_LOG(LogTemp, Warning, TEXT("[YUFS] 시뮬레이션이 진행 중일 때는 화재를 전환할 수 없습니다. 먼저 Stop 하세요."));
		return;
	}

	if (!FireOptions.IsValidIndex(OptionIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("[YUFS] 잘못된 화재 인덱스 %d (등록된 화재 %d개)."), OptionIndex, FireOptions.Num());
		return;
	}

	if (CurrentFireOptionIndex == OptionIndex)
	{
		return;
	}

	ApplyFireOption(OptionIndex);

	UE_LOG(LogTemp, Warning, TEXT("[YUFS] 화재 전환 → [%d] %s"), OptionIndex, *FireOptions[OptionIndex].DisplayName);
}

void AYUFSSimulationController::SelectFireScenario(EFireScenario NewScenario)
{
	SelectFireOption(NewScenario == EFireScenario::ScenarioA ? 0 : 1);
}

TArray<FString> AYUFSSimulationController::GetFireOptionNames() const
{
	TArray<FString> Names;
	Names.Reserve(FireOptions.Num());
	for (int32 Index = 0; Index < FireOptions.Num(); ++Index)
	{
		Names.Add(GetFireOptionName(Index));
	}
	return Names;
}

FString AYUFSSimulationController::GetFireOptionName(int32 OptionIndex) const
{
	if (!FireOptions.IsValidIndex(OptionIndex))
	{
		return FString();
	}
	const FString& DisplayName = FireOptions[OptionIndex].DisplayName;
	return DisplayName.IsEmpty() ? FString::Printf(TEXT("Fire %d"), OptionIndex + 1) : DisplayName;
}

// ─────────────────────────────────────────────────────────────────────────────
// 내부 로직
// ─────────────────────────────────────────────────────────────────────────────

void AYUFSSimulationController::ApplyActiveScenario()
{
	UYUFSGameInstance* GI = GetGameInstance<UYUFSGameInstance>();
	if (!GI || !GI->bHasActiveScenario)
	{
		return;
	}

	bHasActiveScenario = true;
	ActiveScenario = GI->ActiveScenario;

	// 스칼라 파라미터 반영 (NPC 수/정책/배치 프리셋은 NPC 스폰 시스템이 ActiveScenario에서 직접 읽습니다.)
	FireStartDelaySeconds          = ActiveScenario.FireStartDelaySeconds;
	MaxSimDurationSeconds          = ActiveScenario.MaxSimDurationSeconds;
	AlarmTriggerOffsetSeconds      = ActiveScenario.AlarmTriggerOffsetSeconds;
	PreRecordedMsgOffsetSeconds    = ActiveScenario.PreRecordedMsgOffsetSeconds;
	LiveAnnouncementOffsetSeconds  = ActiveScenario.LiveAnnouncementOffsetSeconds;
	StaffGuidanceOffsetSeconds     = ActiveScenario.StaffGuidanceOffsetSeconds;
	bEnableTimelineRecording       = ActiveScenario.bEnableTimelineRecording;
	TimelineRecordEndFireSeconds   = ActiveScenario.TimelineRecordEndFireSeconds;
	TimelineRecordIntervalSeconds  = FMath::Max(0.05f, ActiveScenario.TimelineRecordIntervalSeconds);

	UE_LOG(LogTemp, Log, TEXT("[YUFS] 시나리오 적용: '%s' | 화재지연 %.0fs"),
		*ActiveScenario.DisplayName.ToString(), FireStartDelaySeconds);
}

void AYUFSSimulationController::FindFireSceneActors()
{
	if (!GetWorld()) return;

	int32 VolumeCount = 0;
	for (TActorIterator<AYUFSHeterogeneousVolume> It(GetWorld()); It; ++It)
	{
		if (!*It) continue;
		++VolumeCount;
		if (!HeterogeneousVolume)
		{
			HeterogeneousVolume = *It;
		}
	}

	int32 ManagerCount = 0;
	for (TActorIterator<AYUFSBinaryManager> It(GetWorld()); It; ++It)
	{
		if (!*It) continue;
		++ManagerCount;
		if (!BinaryManager)
		{
			BinaryManager = *It;
		}
	}

	if (BinaryManager && HeterogeneousVolume)
	{
		BinaryManager->SetHeterogeneousVolume(HeterogeneousVolume);
	}

	if (VolumeCount == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[YUFS] 레벨에 AYUFSHeterogeneousVolume 액터가 없습니다. 화재 볼륨을 하나 배치하세요."));
	}
	if (ManagerCount == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[YUFS] 레벨에 AYUFSBinaryManager 액터가 없습니다. 데이터 매니저를 하나 배치하세요."));
	}
	if (VolumeCount > 1 || ManagerCount > 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("[YUFS] HeterogeneousVolume %d개 / BinaryManager %d개가 발견되었습니다 — 화재 전환은 이제 FireOptions + 콤보박스로 하므로 각각 1개씩만 있으면 됩니다. 남는 액터는 삭제하세요."),
			VolumeCount, ManagerCount);
	}
}

void AYUFSSimulationController::ApplyFireOption(int32 OptionIndex)
{
	if (!FireOptions.IsValidIndex(OptionIndex))
	{
		return;
	}

	CurrentFireOptionIndex = OptionIndex;
	const FYUFSFireOption& Option = FireOptions[OptionIndex];

	if (BinaryManager)
	{
		BinaryManager->LoadBinaryFile(Option.BinaryFilePath);
	}

	if (HeterogeneousVolume)
	{
		// 화재마다 FDS 도메인 크기/비율이 달라서, 머티리얼과 함께 이 화재 전용으로
		// 맞춰둔 위치/회전/크기도 같이 적용해야 건물과 어긋나지 않습니다.
		// 단, 새 vdb를 뷰포트에서 눈으로 맞춰보는 동안에는 bAutoApplyFireVolumeTransform을
		// false로 꺼서 이 강제 적용을 건너뛸 수 있습니다.
		if (bAutoApplyFireVolumeTransform)
		{
			HeterogeneousVolume->SetActorTransform(Option.VolumeTransform);
		}
		HeterogeneousVolume->SetFireMaterial(Option.FireMaterial);
		HeterogeneousVolume->SetIgnitionMetadataFile(Option.IgnitionMetadataFile);
		HeterogeneousVolume->ResetFire();
	}

	if (BinaryManager && HeterogeneousVolume)
	{
		BinaryManager->SetHeterogeneousVolume(HeterogeneousVolume);
	}

	if (TimelineRecorder)
	{
		TimelineRecorder->Initialize(this, HeterogeneousVolume);
	}
}

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

		// 일상 산책 중 우연히 출구 근처를 지나가는 것은 대피 성공으로 보지 않습니다.
		const EYUFSBehaviorState State = SM->GetCurrentState();
		if (State != EYUFSBehaviorState::Evacuating && State != EYUFSBehaviorState::Crawling)
		{
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

FSimRunResult AYUFSSimulationController::BuildRunResult() const
{
	FSimRunResult Result;
	Result.RunIndex = CurrentRunIndex;
	// Resolved NPCs remain registered for timeline playback, so they are not
	// additional participants. Removed unresolved NPCs are excluded on unregister.
	Result.TotalNPCCount = FMath::Max(InitialNPCCount, LiveEvacuatedCount + LiveIncapacitatedCount);
	Result.EvacuatedCount = LiveEvacuatedCount;
	Result.IncapacitatedCount = LiveIncapacitatedCount;
	Result.EvacuationRate = Result.TotalNPCCount > 0
		? (float)Result.EvacuatedCount / (float)Result.TotalNPCCount
		: 0.f;
	Result.SimDurationSeconds = ElapsedSimTime;
	Result.AverageEvacuationTime = Result.EvacuatedCount > 0
		? TotalEvacuationTime / static_cast<float>(Result.EvacuatedCount)
		: 0.f;
	Result.RandomSeed = CurrentRunSeed;
	Result.Timestamp = FDateTime::Now();
	Result.ScenarioConfig = CaptureScenarioSnapshot();
	Result.InitialNPCTransforms = InitialNPCTransforms;
	Result.InitialNPCClasses = InitialNPCClasses;
	return Result;
}

FYUFSScenarioConfig AYUFSSimulationController::CaptureScenarioSnapshot() const
{
	// 메뉴에서 넘어온 시나리오가 있으면 그걸 베이스로(맵/NPC수/정책 등 포함),
	// 없으면(메뉴 없이 바로 PIE) 빈 기본값에서 시작합니다.
	FYUFSScenarioConfig Snapshot = bHasActiveScenario ? ActiveScenario : FYUFSScenarioConfig();

	// 스칼라 파라미터는 항상 이 회차가 실제로 사용한 현재 값으로 덮어써서,
	// 메뉴 없이 시작한 경우에도(EditAnywhere 기본값 사용 시) 정확한 값이 들어가게 합니다.
	Snapshot.FireStartDelaySeconds         = FireStartDelaySeconds;
	Snapshot.MaxSimDurationSeconds         = MaxSimDurationSeconds;
	Snapshot.AlarmTriggerOffsetSeconds     = AlarmTriggerOffsetSeconds;
	Snapshot.PreRecordedMsgOffsetSeconds   = PreRecordedMsgOffsetSeconds;
	Snapshot.LiveAnnouncementOffsetSeconds = LiveAnnouncementOffsetSeconds;
	Snapshot.StaffGuidanceOffsetSeconds    = StaffGuidanceOffsetSeconds;
	Snapshot.bEnableTimelineRecording      = bEnableTimelineRecording;
	Snapshot.TimelineRecordEndFireSeconds  = TimelineRecordEndFireSeconds;
	Snapshot.TimelineRecordIntervalSeconds = TimelineRecordIntervalSeconds;

	if (!bHasActiveScenario)
	{
		Snapshot.NPCCount = InitialNPCCount;
	}

	return Snapshot;
}

void AYUFSSimulationController::InitializeRunSeed()
{
	UYUFSGameInstance* GI = GetGameInstance<UYUFSGameInstance>();

	if (GI && GI->bHasPendingReplaySeed)
	{
		// 결과 화면에서 "이 회차 재현"으로 넘어온 경우: 그 회차의 시드를 그대로 씁니다.
		CurrentRunSeed = GI->PendingReplaySeed;
		ApplyReplayNPCTransforms(GI->PendingReplayNPCTransforms, GI->PendingReplayNPCClasses);
		GI->bHasPendingReplaySeed = false;
		GI->PendingReplayNPCTransforms.Reset();
		GI->PendingReplayNPCClasses.Reset();
	}
	else
	{
		CurrentRunSeed = FMath::RandRange(0, MAX_int32 - 1);
	}

	FMath::RandInit(CurrentRunSeed);

	// 재현으로 위치를 되돌린 경우 그 값을, 아니면 레벨에 배치된 현재 값을 이번 회차의
	// "시작 시점" 스냅샷으로 캡처합니다. BuildRunResult()가 이걸 그대로 결과에 담습니다.
	CaptureInitialNPCTransforms();
}

void AYUFSSimulationController::CaptureInitialNPCTransforms()
{
	InitialNPCTransforms.Reset(RegisteredNPCs.Num());
	InitialNPCClasses.Reset(RegisteredNPCs.Num());
	for (AYUFSEvacuationNPC* NPC : RegisteredNPCs)
	{
		InitialNPCTransforms.Add(IsValid(NPC) ? NPC->GetActorTransform() : FTransform::Identity);
		InitialNPCClasses.Add(IsValid(NPC) ? NPC->GetClass() : nullptr);
	}
}

void AYUFSSimulationController::ApplyReplayNPCTransforms(const TArray<FTransform>& Transforms, const TArray<TSubclassOf<AYUFSEvacuationNPC>>& Classes)
{
	// 레벨에 원래부터 배치된(=레벨 리로드 후에도 살아남는) NPC는 여기 먼저 등록되어 있으므로
	// 그 범위까지는 위치만 되돌립니다.
	const int32 ExistingCount = FMath::Min(Transforms.Num(), RegisteredNPCs.Num());
	for (int32 Index = 0; Index < ExistingCount; ++Index)
	{
		if (IsValid(RegisteredNPCs[Index]))
		{
			RegisteredNPCs[Index]->SetActorTransform(Transforms[Index], false, nullptr, ETeleportType::TeleportPhysics);
		}
	}

	// 배치 도구로 런타임에 추가 배치했던 NPC는 레벨 자체엔 저장되지 않아 리로드 후
	// 존재하지 않으므로, 저장해둔 클래스로 다시 스폰해서 채워 넣습니다.
	// (스폰된 NPC는 자신의 BeginPlay에서 RegisterNPC()를 호출해 자동으로 등록됩니다.)
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	for (int32 Index = RegisteredNPCs.Num(); Index < Transforms.Num(); ++Index)
	{
		if (TSubclassOf<AYUFSEvacuationNPC> NPCClass = Classes.IsValidIndex(Index) ? Classes[Index] : nullptr)
		{
			GetWorld()->SpawnActor<AYUFSEvacuationNPC>(NPCClass, Transforms[Index], Params);
		}
	}
}

void AYUFSSimulationController::StoreRunResult(const FSimRunResult& Result)
{
	AllRunResults.Add(Result);
	OnRunCompleted.Broadcast(Result);

	// 결과 화면(UYUFSResultsWidget 등)이 읽을 수 있도록 GameInstance에 회차 결과를 보존합니다.
	if (UYUFSGameInstance* GI = GetGameInstance<UYUFSGameInstance>())
	{
		GI->StoreRunResults(AllRunResults);
	}
}

void AYUFSSimulationController::FinalizeRun()
{
	if (CurrentPhase == ESimPhase::Completed) return;
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

	const FSimRunResult Result = BuildRunResult();
	StoreRunResult(Result);

	UE_LOG(LogTemp, Warning,
		TEXT("[YUFS] Run Done | Evacuated: %d/%d (%.1f%%) | Incapacitated: %d | Duration: %.1fs"),
		Result.EvacuatedCount, Result.TotalNPCCount,
		Result.EvacuationRate * 100.f,
		Result.IncapacitatedCount,
		Result.SimDurationSeconds);
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

	// 타임라인 기록 종료 시점의 대피 결과를 결과 화면용으로 확정해 둡니다.
	// (기록 모드에서는 FinalizeRun()이 호출되지 않고 곧바로 관찰 모드로 들어가므로,
	//  여기서 저장하지 않으면 LastRunResults가 비어 있어 결과 화면에 아무것도 뜨지 않습니다.)
	StoreRunResult(BuildRunResult());

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
		}
		else if (CurrentPhase == ESimPhase::FireStartDelay || CurrentPhase == ESimPhase::FireActive)
		{
			++InitialNPCCount;
		}
	}
}

void AYUFSSimulationController::UnregisterNPC(AYUFSEvacuationNPC* NPC)
{
	if (NPC && RegisteredNPCs.Contains(NPC))
	{
		RegisteredNPCs.Remove(NPC);
		if (CurrentPhase == ESimPhase::WaitingToStart)
		{
			ResolvedNPCs.Remove(NPC);
			InitialNPCCount = RegisteredNPCs.Num();
		}
		else if (!ResolvedNPCs.Contains(NPC))
		{
			// Deletion/cancellation is not an evacuation or an injury. Exclude
			// unresolved withdrawals so they cannot prevent completion forever.
			InitialNPCCount = FMath::Max(LiveEvacuatedCount + LiveIncapacitatedCount, InitialNPCCount - 1);
		}
		// Keep resolved identities as historical results if their render actor
		// is later deleted; this set is compared only, never dereferenced.
	}
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
	if (!PC || HUDWidgetInstance) return;
	if (!HUDWidgetClass)
	{
		HUDWidgetClass = LoadClass<UUserWidget>(nullptr, TEXT("/Game/Blueprint/UI/WBP_SimHUD.WBP_SimHUD_C"));
	}
	if (!HUDWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[YUFS][Merge] Missing JJW HUD /Game/Blueprint/UI/WBP_SimHUD. Preserve JJW UI assets and their redirectors; no legacy simulation HUD is substituted."));
		return;
	}

	PC->bShowMouseCursor = true;
	PC->SetInputMode(FInputModeGameAndUI());

	HUDWidgetInstance = CreateWidget<UUserWidget>(PC, HUDWidgetClass);
	if (HUDWidgetInstance)
	{
		HUDWidgetInstance->AddToViewport();
	}
}

void AYUFSSimulationController::StartNPCActionAnimationShowcase()
{
	UE_LOG(LogTemp, Display, TEXT("[YUFS][Merge] Legacy animation showcase is disabled. NPC placement and camera remain under JJW/player control."));
}

void AYUFSSimulationController::StopNPCActionAnimationShowcase()
{
	for (AYUFSEvacuationNPC* NPC : RegisteredNPCs)
	{
		if (IsValid(NPC)) NPC->ClearActionAnimationPreview();
	}
}
