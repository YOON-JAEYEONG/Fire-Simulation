// Fill out your copyright notice in the Description page of Project Settings.

#include "Simulation/YUFSSimHUD.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Simulation/YUFSGameInstance.h"
#include "Fire/YUFSHeterogeneousVolume.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "NPC/Navigation/YUFSLocalMovementComponent.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "NPC/Navigation/YUFSExitKnowledgeComponent.h"

namespace
{
void RestoreSimulationInput(APlayerController* PC)
{
 if (!PC) return;
 FInputModeGameAndUI InputMode;
 InputMode.SetHideCursorDuringCapture(false);
 PC->SetInputMode(InputMode);
 PC->bShowMouseCursor = true;
 UWidgetBlueprintLibrary::SetFocusToGameViewport();
}
}

int32 UYUFSSimHUD::PaintBelongingsMarkers(const FGeometry& Geometry, FSlateWindowElementList& Elements, int32 Layer) const
{
	static const bool bEnabled = FParse::Param(FCommandLine::Get(), TEXT("YUFSFireAwarenessAudit"));
	if (!bEnabled || !IsValid(SimController) || !GetWorld() || SimController->GetCurrentPhase() != ESimPhase::FireActive) return Layer;
	APlayerController* PC = GetOwningPlayer() ? GetOwningPlayer() : GetWorld()->GetFirstPlayerController();
	if (!PC) return Layer;
	const FVector2D Size = Geometry.GetLocalSize();
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 12);
	TArray<FVector2D, TInlineAllocator<8>> Placed;
	for (TActorIterator<AYUFSEvacuationNPC> It(GetWorld()); It; ++It)
	{
		const AYUFSEvacuationNPC& NPC = **It;
		if (NPC.IsHidden() || !NPC.IsReturningForBelongings()) continue;
		FVector2D Pos;
		if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, NPC.GetActorLocation()+FVector(0,0,115), Pos, true)
			|| Pos.X<125 || Pos.Y<18 || Pos.X>Size.X-145 || Pos.Y>Size.Y-24) continue;
		if (Placed.ContainsByPredicate([&](const FVector2D& Other) { return FMath::Abs(Other.X-Pos.X)<142 && FMath::Abs(Other.Y-Pos.Y)<20; })) continue;
		Placed.Add(Pos);
		const FString Name = NPC.GetName();
		int32 Separator = INDEX_NONE;
		Name.FindLastChar(TCHAR('_'), Separator);
		const FString Id = Separator == INDEX_NONE ? FString::FromInt(NPC.GetStableNPCId()) : Name.Mid(Separator+1);
		const FString Label = FString::Printf(TEXT("#%s %s"), *Id,
			NPC.IsPickingUpBelongings() ? TEXT("가방 챙김") : TEXT("가방 찾으러 돌아감"));
		FSlateDrawElement::MakeBox(Elements, Layer+1, Geometry.ToPaintGeometry(FVector2f(141,20), FSlateLayoutTransform(FVector2f(Pos-FVector2D(4,2)))),
			White, ESlateDrawEffect::None, FLinearColor(.018f,.035f,.06f,.93f));
		FSlateDrawElement::MakeText(Elements, Layer+2, Geometry.ToPaintGeometry(FVector2f(1,1), FSlateLayoutTransform(FVector2f(Pos))),
			Label, Font, ESlateDrawEffect::None, FLinearColor(.96f,.70f,1.f));
	}
	return Placed.IsEmpty() ? Layer : Layer+2;
}

int32 UYUFSSimHUD::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
	FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	Layer = Super::NativePaint(Args, Geometry, Culling, Elements, Layer, Style, bParentEnabled);
	static const bool bEnabled = FParse::Param(FCommandLine::Get(), TEXT("YUFSFireAwarenessAudit"));
	if (!bEnabled || !IsValid(SimController) || !GetWorld()) return Layer;
	const ESimPhase Phase = SimController->GetCurrentPhase();
	// Live diagnosis must never be shown as if it belonged to a historical replay frame.
	if (Phase == ESimPhase::TimelineReview || Phase == ESimPhase::Completed) return Layer;
	APlayerController* PC = GetOwningPlayer() ? GetOwningPlayer() : GetWorld()->GetFirstPlayerController();
	if (!PC) return Layer;

	const TCHAR* Labels[] = {TEXT("미인지"), TEXT("상황 확인"), TEXT("대피 준비"), TEXT("대피 중"),
		TEXT("정체 / 우회"), TEXT("상호작용"), TEXT("행동 불능"), TEXT("이동 대기"), TEXT("가방 회수")};
	const FLinearColor Colors[] = {
		FLinearColor(.78f,.86f,.93f), FLinearColor(1.f,.8f,.23f), FLinearColor(1.f,.62f,.32f),
		FLinearColor(.45f,1.f,.72f), FLinearColor(1.f,.42f,.42f), FLinearColor(.56f,.83f,1.f), FLinearColor(1.f,.55f,.76f),
		FLinearColor(.72f,.82f,1.f), FLinearColor(.96f,.70f,1.f)};
	int32 Counts[UE_ARRAY_COUNT(Labels)] = {};
	int32 WithKnownDoors=0, Exploring=0;
	const FVector2D Size = Geometry.GetLocalSize();
	const FVector2D Panel(FMath::Max(140., Size.X - 291.), 14.);
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 12);
	auto Box = [&](FVector2D Pos, FVector2D Dimensions, FLinearColor Color)
	{
		FSlateDrawElement::MakeBox(Elements, Layer + 1, Geometry.ToPaintGeometry(FVector2f(Dimensions), FSlateLayoutTransform(FVector2f(Pos))),
			White, ESlateDrawEffect::None, Color);
	};
	auto Text = [&](FVector2D Pos, const FString& Say, FLinearColor Color)
	{
		FSlateDrawElement::MakeText(Elements, Layer + 2, Geometry.ToPaintGeometry(FVector2f(1, 1), FSlateLayoutTransform(FVector2f(Pos))),
			Say, Font, ESlateDrawEffect::None, Color);
	};

	TArray<FVector2D, TInlineAllocator<32>> Markers;
	for (TActorIterator<AYUFSEvacuationNPC> It(GetWorld()); It; ++It)
	{
		const AYUFSEvacuationNPC& NPC = **It;
		const auto* Mind = NPC.GetBehaviorStateMachine();
		if (NPC.IsHidden() || !Mind) continue;
		const auto& Audit = NPC.GetFireAwarenessAudit();
		const auto State = Mind->GetCurrentState();
		const auto* Nav = NPC.GetNavigator();
		const auto* Traffic = NPC.GetLocalMovement();
		int32 Status = Audit.FirstCueAt < 0.f ? 0 : 1;
		if (State == EYUFSBehaviorState::Incapacitated) Status = 6;
		else if (NPC.IsReturningForBelongings()) Status = 8;
		else if (State == EYUFSBehaviorState::Preparing) Status = 2;
		else if (Mind->HasCommittedToEvacuation())
		{
			const bool bBlocked = (Nav && Nav->GetNavigationStatus() == EYUFSNavigationStatus::Failed)
				|| (Traffic && Traffic->GetState() != EYUFSLocalMovementState::Following);
			const bool bMoving = NPC.GetVelocity().SizeSquared2D() > FMath::Square(10.0);
			Status = NPC.IsInteractionHoldingPosition() || State == EYUFSBehaviorState::Helping ? 5 : bBlocked ? 4 : bMoving ? 3 : 7;
		}
		++Counts[Status];
		if (Status == 8) continue; // The dedicated marker is painted above the diagnostic labels.
		if (const auto* Knowledge=NPC.GetExitKnowledge())
		{
			WithKnownDoors+=Knowledge->GetKnownDoorCount()>0;
			Exploring+=Knowledge->IsSearchingForExit();
		}
		FVector2D Pos;
		const FVector Head = NPC.GetActorLocation() + FVector(0, 0, 115);
		if (!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC, Head, Pos, true)
			|| Pos.X < 125 || Pos.Y < 18 || Pos.X > Size.X - 144 || Pos.Y > Size.Y - 24
			|| (Pos.X > Panel.X - 140 && Pos.Y < Panel.Y + 151))
			continue;
		const auto Overlaps = [&Pos](const FVector2D& P) { return FMath::Abs(P.X - Pos.X) < 140 && FMath::Abs(P.Y - Pos.Y) < 20; };
		if (Markers.ContainsByPredicate(Overlaps)) continue;
		Markers.Add(Pos);
		const FString NpcName = NPC.GetName();
		int32 Separator = INDEX_NONE;
		NpcName.FindLastChar(TCHAR('_'), Separator);
		const FString Suffix = Separator == INDEX_NONE ? FString::FromInt(NPC.GetStableNPCId()) : NpcName.Mid(Separator + 1);
		const FString Cue = Audit.FirstCue == TEXT("Alarm") ? TEXT("경보") :
			(Audit.FirstCue == TEXT("Smoke") ? TEXT("연기") : (Audit.FirstCue == TEXT("Heat") ? TEXT("열") : TEXT("")));
		const FString Title = FString::Printf(TEXT("#%s %s%s%s"), *Suffix, Labels[Status], Cue.IsEmpty() ? TEXT("") : TEXT(" · "), *Cue);
		Box(Pos - FVector2D(4, 2), FVector2D(141, 20), FLinearColor(.018f,.035f,.06f,.93f));
		Text(Pos, Title, Colors[Status]);
	}
	Box(Panel, FVector2D(277, 141), FLinearColor(.018f,.035f,.06f,.94f));
	Text(Panel + FVector2D(10, 6), TEXT("NPC 화재 인지 · 실시간 검증"), FLinearColor::White);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Labels); ++Index)
	{
		Text(Panel + FVector2D(10 + (Index % 2) * 133, 26 + (Index / 2) * 18),
			FString::Printf(TEXT("%s %d"), Labels[Index], Counts[Index]), Colors[Index]);
	}
	Text(Panel + FVector2D(10, 119), FString::Printf(TEXT("문 기억 %d명  ·  통로 탐색 %d명"), WithKnownDoors, Exploring),
		FLinearColor(.56f,.83f,1.f));
	return PaintBelongingsMarkers(Geometry, Elements, Layer + 2);
}

TMap<FName, FText> UYUFSSimHUD::GetDisplayedTextSnapshot() const
{
	TMap<FName, FText> Result;
	if (WidgetTree)
	{
		WidgetTree->ForEachWidget([&Result](UWidget* Widget)
		{
			if (const UTextBlock* Text = Cast<UTextBlock>(Widget))
			{
				Result.Add(Text->GetFName(), Text->GetText());
			}
		});
	}
	return Result;
}

void UYUFSSimHUD::NativeConstruct()
{
	Super::NativeConstruct();
	FindSimController();
	FindFirePoints();
	PopulateFireOptions();
}

void UYUFSSimHUD::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 컨트롤러가 없으면 매 틱마다 재탐색
	if (!SimController)
	{
		FindSimController();
	}
}

void UYUFSSimHUD::FindSimController()
{
	if (!GetWorld()) return;
	for (TActorIterator<AYUFSSimulationController> It(GetWorld()); It; ++It)
	{
		SimController = *It;
		break;
	}
}

void UYUFSSimHUD::FindFirePoints()
{
	if (!GetWorld()) return;

	// 레벨에 배치된 YUFSHeterogeneousVolume 액터들 중, 에디터 표시 이름(Actor Label)이
	// "_A"/"_B"로 끝나는 액터를 각각 FirePointA / FirePointB로 매칭합니다.
	// 주의: GetName()(내부 오브젝트 이름)이 아니라 GetActorLabel()(에디터에 보이는 이름)을
	// 써야 합니다 — 월드 파티션 액터는 GetName()이 "_UAID_..." 형태의 자동 생성된
	// GUID라서 "_A"/"_B" 매칭이 안 됩니다.
	for (TActorIterator<AYUFSHeterogeneousVolume> It(GetWorld()); It; ++It)
	{
		AYUFSHeterogeneousVolume* Volume = *It;
		if (!Volume) continue;

#if WITH_EDITOR
		const FString Name = Volume->GetActorLabel();
#else
		const FString Name = Volume->GetName();
#endif
		// 디버그: 월드에서 발견된 HeterogeneousVolume 액터 이름을 전부 찍어봅니다.
		UE_LOG(LogTemp, Warning, TEXT("[YUFSSimHUD] Found HeterogeneousVolume: Label='%s' Name='%s'"), *Name, *Volume->GetName());

		if (Name.EndsWith(TEXT("_A")))
		{
			FirePointA = Volume;
		}
		else if (Name.EndsWith(TEXT("_B")))
		{
			FirePointB = Volume;
		}
	}

	if (!FirePointA)
	{
		UE_LOG(LogTemp, Warning, TEXT("[YUFSSimHUD] FirePointA를 월드에서 찾지 못했습니다."));
	}
	if (!FirePointB)
	{
		UE_LOG(LogTemp, Warning, TEXT("[YUFSSimHUD] FirePointB를 월드에서 찾지 못했습니다."));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 버튼 핸들러
// ─────────────────────────────────────────────────────────────────────────────

void UYUFSSimHUD::OnStartButtonClicked()
{
	RestoreSimulationInput(GetOwningPlayer());
	if (!SimController) return;

	const ESimPhase Phase = SimController->GetCurrentPhase();
	if (Phase == ESimPhase::WaitingToStart)
	{
		SimController->StartSimulation();
	}
	else
	{
		SimController->ResumeSimulation();
	}
}

void UYUFSSimHUD::OnPauseButtonClicked()
{
	if (SimController) SimController->PauseSimulation();
}

void UYUFSSimHUD::OnStopButtonClicked()
{
	if (SimController) SimController->StopAndResetSimulation();
}

void UYUFSSimHUD::OnMainMenuButtonClicked()
{
	if (UYUFSGameInstance* GI = Cast<UYUFSGameInstance>(GetGameInstance()))
	{
		GI->ReturnToMainMenu();
	}
}

void UYUFSSimHUD::PopulateFireOptions()
{
	if (!SimController) return;

	const TArray<FString> Names = SimController->GetFireOptionNames();

	if (FireSelectComboBox)
	{
		FireSelectComboBox->ClearOptions();
		for (const FString& Name : Names)
		{
			FireSelectComboBox->AddOption(Name);
		}

		const int32 ActiveIndex = SimController->GetActiveFireOptionIndex();
		if (Names.IsValidIndex(ActiveIndex))
		{
			// ESelectInfo::Direct로 들어오므로 아래 OnFireOptionComboBoxChanged가
			// 다시 SelectFireOption을 호출하는 피드백 루프는 생기지 않습니다.
			FireSelectComboBox->SetSelectedOption(Names[ActiveIndex]);
		}

		FireSelectComboBox->OnSelectionChanged.RemoveDynamic(this, &UYUFSSimHUD::OnFireOptionComboBoxChanged);
		FireSelectComboBox->OnSelectionChanged.AddDynamic(this, &UYUFSSimHUD::OnFireOptionComboBoxChanged);
	}
}

void UYUFSSimHUD::OnFireOptionComboBoxChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	// PopulateFireOptions()의 SetSelectedOption() 호출 자체가 발생시키는 이벤트(Direct)는 무시합니다.
	if (SelectionType == ESelectInfo::Direct) return;
	if (!SimController) return;

	const TArray<FString> Names = SimController->GetFireOptionNames();
	const int32 Index = Names.IndexOfByKey(SelectedItem);
	if (Index != INDEX_NONE)
	{
		SimController->SelectFireOption(Index);
	}
}

void UYUFSSimHUD::OnFireOptionSelected(int32 OptionIndex)
{
	if (SimController)
	{
		SimController->SelectFireOption(OptionIndex);
	}
}

TArray<FString> UYUFSSimHUD::GetFireOptionNames() const
{
	return SimController ? SimController->GetFireOptionNames() : TArray<FString>();
}

int32 UYUFSSimHUD::GetActiveFireOptionIndex() const
{
	return SimController ? SimController->GetActiveFireOptionIndex() : INDEX_NONE;
}

void UYUFSSimHUD::OnFireSceneAButtonClicked()
{
	OnFireOptionSelected(0);
}

void UYUFSSimHUD::OnFireSceneBButtonClicked()
{
	OnFireOptionSelected(1);
}

bool UYUFSSimHUD::IsFireSceneASelected() const
{
	return GetActiveFireOptionIndex() == 0;
}

bool UYUFSSimHUD::IsFireSceneBSelected() const
{
	return GetActiveFireOptionIndex() == 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// 데이터 조회 (UMG 바인딩용)
// ─────────────────────────────────────────────────────────────────────────────

FText UYUFSSimHUD::GetPhaseText() const
{
	if (!SimController) return FText::FromString(TEXT("--"));

	switch (SimController->GetCurrentPhase())
	{
	case ESimPhase::WaitingToStart: return FText::FromString(SimController->IsWaitingForInteractionPreview()
		? TEXT("상호작용 준비 중 · 시작 클릭 예약 가능") : TEXT("대기 중"));
	case ESimPhase::FireStartDelay: return FText::FromString(TEXT("화재 발생 전"));
	case ESimPhase::FireActive:     return FText::FromString(TEXT("🔥 화재 진행 중"));
	case ESimPhase::TimelineReview: return FText::FromString(TEXT("타임라인 관찰 모드"));
	case ESimPhase::Completed:      return FText::FromString(TEXT("✅ 시뮬레이션 완료"));
	default:                        return FText::FromString(TEXT("--"));
	}
}

FText UYUFSSimHUD::GetElapsedTimeText() const
{
	if (!SimController) return FText::FromString(TEXT("0:00"));

	const auto Stats = SimController->GetDisplayStats();
	if (!Stats.bHasData) return FText::FromString(TEXT("--:--"));
	const float Elapsed = FMath::Max(0.f, Stats.ElapsedSimTime);
	const int32 Minutes = FMath::FloorToInt(Elapsed / 60.f);
	const int32 Seconds = FMath::FloorToInt(FMath::Fmod(Elapsed, 60.f));
	return FText::FromString(FString::Printf(TEXT("%d:%02d"), Minutes, Seconds));
}

FText UYUFSSimHUD::GetFireCountdownText() const
{
	if (!SimController) return FText::FromString(TEXT(""));

	const float Countdown = SimController->GetFireStartCountdown();
	if (Countdown <= 0.f) return FText::FromString(TEXT(""));
	return FText::FromString(FString::Printf(TEXT("화재까지 %.0f초"), Countdown));
}

FText UYUFSSimHUD::GetNPCStatusText() const
{
	if (!SimController) return FText::FromString(TEXT("NPC: -/-"));
	const auto Stats = SimController->GetDisplayStats();
	if (!Stats.bHasData) return FText::FromString(TEXT("기록 없음"));

	return FText::FromString(FString::Printf(
		TEXT("대피: %d | 행동 불능: %d | 전체: %d"),
		Stats.EvacuatedCount,
		Stats.IncapacitatedCount,
		Stats.TotalNPCCount));
}

FText UYUFSSimHUD::GetRunProgressText() const
{
	if (!SimController) return FText::FromString(TEXT(""));

	const int32 Current = SimController->GetCurrentRunIndex();
	return FText::FromString(FString::Printf(TEXT("실험 %d회차"), Current));
}

float UYUFSSimHUD::GetEvacuationRatePercent() const
{
	if (!SimController) return 0.f;

	const auto Stats = SimController->GetDisplayStats();
	if (!Stats.bHasData || Stats.TotalNPCCount <= 0) return 0.f;
	return FMath::Clamp(100.f * float(Stats.EvacuatedCount) / float(Stats.TotalNPCCount), 0.f, 100.f);
}

bool UYUFSSimHUD::IsSimulationActive() const
{
	if (!SimController) return false;
	const ESimPhase Phase = SimController->GetCurrentPhase();
	return Phase == ESimPhase::FireStartDelay || Phase == ESimPhase::FireActive;
}

bool UYUFSSimHUD::IsFireActive() const
{
	if (!SimController) return false;
	return SimController->GetCurrentPhase() == ESimPhase::FireActive;
}


// ─────────────────────────────────────────────────────────────────────────────
// 타임라인 관찰 모드 바인딩
// ─────────────────────────────────────────────────────────────────────────────

void UYUFSSimHUD::OnTimelinePlayButtonClicked()
{
	if (SimController)
	{
		SimController->PlayTimeline();
	}
}

void UYUFSSimHUD::OnTimelinePauseButtonClicked()
{
	if (SimController)
	{
		SimController->PauseTimeline();
	}
}

void UYUFSSimHUD::OnTimelineSliderChanged(float NormalizedValue)
{
	if (SimController)
	{
		SimController->SeekTimelineByNormalizedValue(NormalizedValue);
	}
}

bool UYUFSSimHUD::IsTimelineReviewMode() const
{
	return SimController && SimController->IsTimelineReviewMode();
}

bool UYUFSSimHUD::IsTimelinePlaying() const
{
	return SimController && SimController->IsTimelinePlaying();
}

float UYUFSSimHUD::GetTimelineProgress01() const
{
	return SimController ? SimController->GetTimelineProgress01() : 0.f;
}

FText UYUFSSimHUD::GetTimelineTimeText() const
{
	if (!SimController)
	{
		return FText::FromString(TEXT("0.0 / 0.0초"));
	}

	return FText::FromString(FString::Printf(
		TEXT("%.1f / %.1f초"),
		SimController->GetTimelineCurrentTime(),
		SimController->GetTimelineMaxTime()));
}

void UYUFSSimHUD::OnCameraOverviewButtonClicked()
{
	UE_LOG(LogTemp, Warning, TEXT("Overview camera button clicked"));

	UWorld* World = GetWorld();
	if (!World) return;

	APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
	if (!PC) return;

	TArray<AActor*> Cameras;
	UGameplayStatics::GetAllActorsWithTag(World, FName("OverviewCamera"), Cameras);

	UE_LOG(LogTemp, Warning, TEXT("OverviewCamera count: %d"), Cameras.Num());

	if (Cameras.Num() > 0 && Cameras[0])
	{
		PC->SetViewTargetWithBlend(Cameras[0], 0.5f);
		UE_LOG(LogTemp, Warning, TEXT("Switched to OverviewCamera"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("OverviewCamera not found"));
	}
}

void UYUFSSimHUD::OnCameraFireZoneButtonClicked()
{
	UE_LOG(LogTemp, Warning, TEXT("Fire zone camera button clicked"));

	UWorld* World = GetWorld();
	if (!World) return;

	APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
	if (!PC) return;

	TArray<AActor*> Cameras;
	UGameplayStatics::GetAllActorsWithTag(World, FName("FireZoneCamera"), Cameras);

	UE_LOG(LogTemp, Warning, TEXT("FireZoneCamera count: %d"), Cameras.Num());

	if (Cameras.Num() > 0 && Cameras[0])
	{
		PC->SetViewTargetWithBlend(Cameras[0], 0.5f);
		UE_LOG(LogTemp, Warning, TEXT("Switched to FireZoneCamera"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("FireZoneCamera not found"));
	}
}

void UYUFSSimHUD::OnCameraPlayerViewButtonClicked()
{
	UE_LOG(LogTemp, Warning, TEXT("Player view button clicked"));

	UWorld* World = GetWorld();
	if (!World) return;

	APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
	if (!PC) return;

	APawn* PlayerPawn = PC->GetPawn();
	if (!PlayerPawn)
	{
		UE_LOG(LogTemp, Error, TEXT("Player pawn not found"));
		return;
	}

	PC->SetViewTargetWithBlend(PlayerPawn, 0.5f);
	RestoreSimulationInput(PC);
	UE_LOG(LogTemp, Warning, TEXT("Switched back to Player View"));
}
