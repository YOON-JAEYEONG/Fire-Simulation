#include "UI/YUFSDropZoneWidget.h"

#include "Application/SlateApplicationBase.h"
#include "Components/CapsuleComponent.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "NavigationSystem.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "Simulation/YUFSSimulationController.h"
#include "UI/YUFSNPCDragDropOperation.h"
#include "UI/YUFSNPCPlacementPreview.h"
#include "UI/YUFSNPCActionWidget.h"
#include "UI/YUFSNPCRotationWidget.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/SViewport.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Blueprint/SlateBlueprintLibrary.h"

void UYUFSDropZoneWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (!SimController)
	{
		for (TActorIterator<AYUFSSimulationController> It(GetWorld()); It; ++It)
		{
			SimController = *It;
			break;
		}
	}

	// 위젯 히트 테스트 문제를 우회하기 위한 타이머 기반 클릭 감지 (항상 실행)
	GetWorld()->GetTimerManager().SetTimer(
		ClickDetectionTimer, this, &UYUFSDropZoneWidget::TryDetectNPCClick, 0.016f, true);
}

void UYUFSDropZoneWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PreviewTickTimer);
		World->GetTimerManager().ClearTimer(ClickDetectionTimer);
	}
	DestroyPreview();
	if (bInRotationMode) ExitRotationMode(false);
	HideNPCActionWidget();
	Super::NativeDestruct();
}

// ─────────────────────────────────────────────────────────────────────────────
// NPC 클릭 감지
// ─────────────────────────────────────────────────────────────────────────────

FReply UYUFSDropZoneWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// 타이머 방식이 주 감지기 — 여기선 액션 위젯 닫기만 처리
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bShowingNPCAction)
	{
		HideNPCActionWidget();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

// 타이머로 매 프레임 마우스 버튼 상태를 폴링 — 위젯 히트 테스트 우회
void UYUFSDropZoneWidget::TryDetectNPCClick()
{
	// Slate에서 직접 마우스 버튼 상태 읽기 (InputMode 무관하게 작동)
	const bool bIsDown = FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
	const bool bJustPressed = bIsDown && !bWasLeftMouseDown;
	bWasLeftMouseDown = bIsDown;

	if (!bJustPressed) return;

	// 다른 모드 중 무시
	if (bInRotationMode || bShowingNPCAction) return;
	if (!SimController || SimController->GetCurrentPhase() != ESimPhase::WaitingToStart) return;

	AYUFSEvacuationNPC* HitNPC = nullptr;
	if (TryGetNPCUnderCursor(HitNPC))
	{
		ShowNPCActionWidget(HitNPC);
	}
}

bool UYUFSDropZoneWidget::TryGetNPCUnderCursor(AYUFSEvacuationNPC*& OutNPC) const
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC) return false;

	UGameViewportClient* ViewportClient = GEngine->GameViewport;
	if (!ViewportClient || !ViewportClient->Viewport) return false;

	// 현재 커서의 뷰포트 픽셀 좌표
	FVector2D CursorViewportPos;
	if (!ViewportClient->GetMousePosition(CursorViewportPos))
	{
		TSharedPtr<SViewport> VPWidget = ViewportClient->GetGameViewportWidget();
		if (!VPWidget.IsValid()) return false;
		FGeometry VPGeom = VPWidget->GetCachedGeometry();
		CursorViewportPos = VPGeom.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());
		CursorViewportPos *= VPGeom.Scale;
	}

	// 콜리전 프리셋에 무관하게 작동:
	// 각 NPC의 월드 위치를 스크린 좌표로 투영해 커서와의 거리로 선택
	const float MaxSelectRadiusPx = 60.f;  // 선택 반경 (픽셀)
	float ClosestDistSq = MaxSelectRadiusPx * MaxSelectRadiusPx;
	OutNPC = nullptr;

	for (TActorIterator<AYUFSEvacuationNPC> It(GetWorld()); It; ++It)
	{
		AYUFSEvacuationNPC* NPC = *It;
		if (!IsValid(NPC) || NPC->IsHidden()) continue;

		FVector2D NPCScreenPos;
		if (!PC->ProjectWorldLocationToScreen(NPC->GetActorLocation(), NPCScreenPos, false))
		{
			continue;
		}

		const float DistSq = FVector2D::DistSquared(CursorViewportPos, NPCScreenPos);
		if (DistSq < ClosestDistSq)
		{
			ClosestDistSq = DistSq;
			OutNPC = NPC;
		}
	}

	return IsValid(OutNPC);
}

static void SetNPCOutline(AYUFSEvacuationNPC* NPC, bool bEnabled)
{
	if (!IsValid(NPC)) return;
	if (USkeletalMeshComponent* Mesh = NPC->GetMesh())
	{
		Mesh->SetRenderCustomDepth(bEnabled);
		Mesh->SetCustomDepthStencilValue(bEnabled ? 1 : 0);
	}
}

void UYUFSDropZoneWidget::ShowNPCActionWidget(AYUFSEvacuationNPC* NPC)
{
	if (!NPCActionWidgetClass) return;

	HideNPCActionWidget();

	SelectedNPC = NPC;
	bShowingNPCAction = true;

	SetNPCOutline(NPC, true);

	NPCActionWidget = CreateWidget<UYUFSNPCActionWidget>(GetOwningPlayer(), NPCActionWidgetClass);
	if (!NPCActionWidget) { HideNPCActionWidget(); return; }

	NPCActionWidget->OnDelete.AddDynamic(this, &UYUFSDropZoneWidget::OnNPCDeleteClicked);
	NPCActionWidget->OnCancel.AddDynamic(this, &UYUFSDropZoneWidget::OnNPCCancelClicked);

	// NPC 위치를 스크린 좌표로 변환해 위젯을 NPC 머리 위에 배치
	FVector2D ScreenPos;
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const FVector NPCHeadPos = NPC->GetActorLocation() + FVector(0.f, 0.f, 120.f);
	if (PC && PC->ProjectWorldLocationToScreen(NPCHeadPos, ScreenPos, false))
	{
		NPCActionWidget->AddToViewport(10);
		NPCActionWidget->SetPositionInViewport(ScreenPos, false);
	}
	else
	{
		NPCActionWidget->AddToViewport(10);
	}
}

void UYUFSDropZoneWidget::HideNPCActionWidget()
{
	SetNPCOutline(SelectedNPC, false);

	if (NPCActionWidget)
	{
		NPCActionWidget->RemoveFromParent();
		NPCActionWidget = nullptr;
	}
	SelectedNPC = nullptr;
	bShowingNPCAction = false;
}

void UYUFSDropZoneWidget::OnNPCDeleteClicked()
{
	if (IsValid(SelectedNPC))
	{
		if (SimController) SimController->UnregisterNPC(SelectedNPC);
		SelectedNPC->Destroy();
		UE_LOG(LogTemp, Log, TEXT("[YUFS] NPC 삭제 완료."));
	}
	HideNPCActionWidget();
}

void UYUFSDropZoneWidget::OnNPCCancelClicked()
{
	HideNPCActionWidget();
}

// ─────────────────────────────────────────────────────────────────────────────
// 드래그 미리보기
// ─────────────────────────────────────────────────────────────────────────────

void UYUFSDropZoneWidget::TickPreviewUpdate()
{
	if (!IsValid(ActivePreview)) return;

	FVector FloorPos;
	const bool bValid = GetCursorWorldLocation(FloorPos, PreviewNPCClass);
	ActivePreview->SetActorHiddenInGame(!bValid);
	if (bValid) ActivePreview->UpdateLocation(FloorPos);
}

void UYUFSDropZoneWidget::NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragEnter(InGeometry, InDragDropEvent, InOperation);
	if (!SimController || SimController->GetCurrentPhase() != ESimPhase::WaitingToStart) return;

	if (bInRotationMode || bShowingNPCAction) return;

	UYUFSNPCDragDropOperation* DragOp = Cast<UYUFSNPCDragDropOperation>(InOperation);
	if (!DragOp || !DragOp->NPCClass) return;

	SpawnPreview(DragOp->NPCClass);

	TickPreviewUpdate();

	GetWorld()->GetTimerManager().SetTimer(
		PreviewTickTimer, this, &UYUFSDropZoneWidget::TickPreviewUpdate, 0.016f, true);
}

void UYUFSDropZoneWidget::NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragLeave(InDragDropEvent, InOperation);
	GetWorld()->GetTimerManager().ClearTimer(PreviewTickTimer);
	DestroyPreview();
}

bool UYUFSDropZoneWidget::NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	if (bInRotationMode || bShowingNPCAction || !SimController
		|| SimController->GetCurrentPhase() != ESimPhase::WaitingToStart) return false;
	return InOperation && InOperation->IsA<UYUFSNPCDragDropOperation>();
}

bool UYUFSDropZoneWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	if (bInRotationMode || bShowingNPCAction) return false;

	GetWorld()->GetTimerManager().ClearTimer(PreviewTickTimer);
	DestroyPreview();

	UYUFSNPCDragDropOperation* DragOp = Cast<UYUFSNPCDragDropOperation>(InOperation);
	if (!DragOp || !DragOp->NPCClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[YUFS|DropZone] 유효하지 않은 DragOp."));
		return false;
	}
	if (!SimController || SimController->GetCurrentPhase() != ESimPhase::WaitingToStart)
	{
		UE_LOG(LogTemp, Warning, TEXT("[YUFS|DropZone] 시뮬레이션 시작 전에만 배치 가능합니다."));
		return false;
	}

	FVector FloorPos;
	const FVector2D DropPosition = InDragDropEvent.GetScreenSpacePosition();
	if (!GetCursorWorldLocation(FloorPos, DragOp->NPCClass, &DropPosition))
	{
		UE_LOG(LogTemp, Warning, TEXT("[YUFS|DropZone] Placement rejected: point at a visible, walkable floor with room for the NPC."));
		return false;
	}

	// NPC 스폰 (BeginPlay에서 SimController에 자동 등록됨)
	AYUFSEvacuationNPC* NPC = SpawnNPC(DragOp->NPCClass, FloorPos);
	if (!IsValid(NPC)) return false;

	EnterRotationMode(NPC);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Rotation 모드
// ─────────────────────────────────────────────────────────────────────────────

void UYUFSDropZoneWidget::EnterRotationMode(AYUFSEvacuationNPC* NPC)
{
	bInRotationMode = true;
	PendingNPC = NPC;
	// WaitingToStart permits everyday wandering. Hold this NPC until placement is confirmed.
	bPendingTickEnabled = NPC->IsActorTickEnabled();
	NPC->SetActorTickEnabled(false);
	if (UCharacterMovementComponent* Movement = NPC->GetCharacterMovement())
	{
		PendingMovementMode = static_cast<uint8>(Movement->MovementMode);
		PendingCustomMovementMode = Movement->CustomMovementMode;
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}

	SetNPCOutline(NPC, true);

	// DropZone 오버레이를 숨겨서 Rotation 위젯 버튼이 반투명하게 보이는 현상 제거
	SetVisibility(ESlateVisibility::Hidden);

	if (RotationWidgetClass)
	{
		RotationWidget = CreateWidget<UYUFSNPCRotationWidget>(GetOwningPlayer(), RotationWidgetClass);
		if (RotationWidget)
		{
			RotationWidget->OnConfirmed.AddDynamic(this, &UYUFSDropZoneWidget::OnRotationConfirmed);
			RotationWidget->OnCancelled.AddDynamic(this, &UYUFSDropZoneWidget::OnRotationCancelled);
			RotationWidget->OnRotationDelta.AddDynamic(this, &UYUFSDropZoneWidget::OnRotationDelta);
			RotationWidget->AddToViewport(10);

			RotationWidget->SetAngleDisplay(NPC->GetActorRotation().Yaw);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[YUFS|DropZone] RotationWidgetClass가 설정되지 않았습니다. WBP_NPCRotation을 연결하세요."));
	}

	if (!RotationWidget)
	{
		ExitRotationMode(false);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[YUFS] Rotation 모드 — 드래그 영역을 좌우로 드래그해 방향을 설정하세요."));
}

void UYUFSDropZoneWidget::ExitRotationMode(bool bConfirm)
{
	SetNPCOutline(PendingNPC, false);

	if (RotationWidget)
	{
		RotationWidget->RemoveFromParent();
		RotationWidget = nullptr;
	}

	if (bConfirm)
	{
		if (IsValid(PendingNPC))
		{
			PendingNPC->SetActorTickEnabled(bPendingTickEnabled);
			if (UCharacterMovementComponent* Movement = PendingNPC->GetCharacterMovement())
				Movement->SetMovementMode(static_cast<EMovementMode>(PendingMovementMode), PendingCustomMovementMode);
		}
		// BeginPlay already registered the NPC.
		UE_LOG(LogTemp, Log, TEXT("[YUFS] NPC '%s' 배치 확정."),
			IsValid(PendingNPC) ? *PendingNPC->GetName() : TEXT("(invalid)"));
	}
	else
	{
		// 취소: 등록 해제 후 파괴
		if (IsValid(PendingNPC) && SimController)
		{
			SimController->UnregisterNPC(PendingNPC);
		}
		if (IsValid(PendingNPC))
		{
			PendingNPC->Destroy();
		}
		UE_LOG(LogTemp, Log, TEXT("[YUFS] NPC 배치 취소."));
	}

	PendingNPC = nullptr;
	bInRotationMode = false;

	// DropZone 오버레이 복원
	SetVisibility(ESlateVisibility::Visible);
}

void UYUFSDropZoneWidget::OnRotationDelta(float DeltaYaw)
{
	if (!IsValid(PendingNPC)) return;

	FRotator Rot = PendingNPC->GetActorRotation();
	Rot.Yaw += DeltaYaw;
	PendingNPC->SetActorRotation(Rot);

	if (RotationWidget)
	{
		RotationWidget->SetAngleDisplay(Rot.Yaw);
	}
}

void UYUFSDropZoneWidget::OnRotationConfirmed()
{
	ExitRotationMode(true);
}

void UYUFSDropZoneWidget::OnRotationCancelled()
{
	ExitRotationMode(false);
}

// ─────────────────────────────────────────────────────────────────────────────
// 위치 계산
// ─────────────────────────────────────────────────────────────────────────────

bool UYUFSDropZoneWidget::GetCursorWorldLocation(FVector& OutWorldPos,
	TSubclassOf<AYUFSEvacuationNPC> NPCClass, const FVector2D* ScreenPosition) const
{
	UWorld* World = GetWorld();
	APlayerController* PC = GetOwningPlayer();
	if (!World || !PC || !NPCClass) return false;
	UGameViewportClient* ViewportClient = World->GetGameViewport();
	if (!ViewportClient || !ViewportClient->Viewport) return false;

	// Drag/drop events use desktop coordinates. Convert through Slate's viewport geometry
	// for both preview and drop, including editor window offsets and DPI scaling.
	FVector2D PixelPosition, ViewportPosition;
	USlateBlueprintLibrary::AbsoluteToViewport(this,
		ScreenPosition ? *ScreenPosition : FSlateApplication::Get().GetCursorPos(),
		PixelPosition, ViewportPosition);
	const FIntPoint Size = ViewportClient->Viewport->GetSizeXY();
	if (PixelPosition.X < 0 || PixelPosition.Y < 0 || PixelPosition.X >= Size.X || PixelPosition.Y >= Size.Y)
		return false;
	FVector RayOrigin, RayDirection;
	if (!PC->DeprojectScreenPositionToWorld(PixelPosition.X, PixelPosition.Y, RayOrigin, RayDirection))
		return false;

	const AYUFSEvacuationNPC* CDO = NPCClass->GetDefaultObject<AYUFSEvacuationNPC>();
	const UCapsuleComponent* Capsule = CDO ? CDO->GetCapsuleComponent() : nullptr;
	const UCharacterMovementComponent* Movement = CDO ? CDO->GetCharacterMovement() : nullptr;
	if (!Capsule || !Movement) return false;

	FCollisionQueryParams Query(SCENE_QUERY_STAT(NPCPlacement), true);
	Query.AddIgnoredActor(PC->GetPawn());
	if (IsValid(ActivePreview)) Query.AddIgnoredActor(ActivePreview);
	FHitResult Hit;
	// Use the first visible surface: never search through a wall or guess another floor.
	if (!World->LineTraceSingleByChannel(Hit, RayOrigin, RayOrigin + RayDirection * 100000.f,
		ECC_Visibility, Query) || !Movement->IsWalkable(Hit)) return false;

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);
	FNavLocation Nav;
	const float Radius = Capsule->GetScaledCapsuleRadius();
	if (!NavSys || !NavSys->ProjectPointToNavigation(Hit.ImpactPoint, Nav,
		FVector(Radius, Radius, 30.f), &Movement->GetNavAgentPropertiesRef())) return false;
	// Reject a projection that moves the drop away from the visible floor.
	if (FVector::DistSquared2D(Hit.ImpactPoint, Nav.Location) > FMath::Square(10.f)
		|| FMath::Abs(Hit.ImpactPoint.Z - Nav.Location.Z) > 30.f) return false;

	const FVector Center = Nav.Location + FVector(0, 0, Capsule->GetScaledCapsuleHalfHeight() + 2.f);
	FCollisionQueryParams Clearance(SCENE_QUERY_STAT(NPCPlacementClearance), false);
	if (IsValid(ActivePreview)) Clearance.AddIgnoredActor(ActivePreview);
	if (World->OverlapBlockingTestByChannel(Center, FQuat::Identity, Capsule->GetCollisionObjectType(),
		FCollisionShape::MakeCapsule(Radius, Capsule->GetScaledCapsuleHalfHeight()), Clearance,
		FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()))) return false;
	OutWorldPos = Nav.Location;
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// NPC 스폰 (등록은 BeginPlay가 담당)
// ─────────────────────────────────────────────────────────────────────────────

AYUFSEvacuationNPC* UYUFSDropZoneWidget::SpawnNPC(TSubclassOf<AYUFSEvacuationNPC> NPCClass, const FVector& FloorLocation)
{
	FVector SpawnLocation = FloorLocation;
	if (AYUFSEvacuationNPC* CDO = Cast<AYUFSEvacuationNPC>(NPCClass->GetDefaultObject()))
	{
		if (const UCapsuleComponent* Cap = CDO->GetCapsuleComponent())
		{
			SpawnLocation.Z += Cap->GetScaledCapsuleHalfHeight() + 2.f;
		}
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;

	AYUFSEvacuationNPC* NPC = GetWorld()->SpawnActor<AYUFSEvacuationNPC>(
		NPCClass, SpawnLocation, FRotator::ZeroRotator, Params);

	if (!IsValid(NPC))
	{
		UE_LOG(LogTemp, Error, TEXT("[YUFS|DropZone] SpawnActor 실패 — %s @ %s"),
			*NPCClass->GetName(), *SpawnLocation.ToString());
	}

	return NPC;
}

// ─────────────────────────────────────────────────────────────────────────────
// 고스트 프리뷰 관리
// ─────────────────────────────────────────────────────────────────────────────

void UYUFSDropZoneWidget::SpawnPreview(TSubclassOf<AYUFSEvacuationNPC> NPCClass)
{
	DestroyPreview();
	PreviewNPCClass = NPCClass;

	TSubclassOf<AYUFSNPCPlacementPreview> SpawnClass =
		PreviewActorClass
		? PreviewActorClass
		: TSubclassOf<AYUFSNPCPlacementPreview>(AYUFSNPCPlacementPreview::StaticClass());

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ActivePreview = GetWorld()->SpawnActor<AYUFSNPCPlacementPreview>(
		SpawnClass, FVector::ZeroVector, FRotator::ZeroRotator, Params);

	if (IsValid(ActivePreview))
	{
		ActivePreview->InitFromNPCClass(NPCClass);
		ActivePreview->SetActorHiddenInGame(true);
	}
}

void UYUFSDropZoneWidget::DestroyPreview()
{
	if (IsValid(ActivePreview))
	{
		ActivePreview->Destroy();
		ActivePreview = nullptr;
	}
}