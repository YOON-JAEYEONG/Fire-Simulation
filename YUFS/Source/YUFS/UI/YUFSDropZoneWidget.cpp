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
#include "UI/YUFSNPCRotationWidget.h"
#include "Widgets/SViewport.h"

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
}

// ─────────────────────────────────────────────────────────────────────────────
// 드래그 미리보기
// ─────────────────────────────────────────────────────────────────────────────

void UYUFSDropZoneWidget::TickPreviewUpdate()
{
	if (!IsValid(ActivePreview)) return;

	FVector FloorPos;
	if (GetCursorWorldLocation(FloorPos))
	{
		ActivePreview->UpdateLocation(FloorPos);
	}
}

void UYUFSDropZoneWidget::NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragEnter(InGeometry, InDragDropEvent, InOperation);

	if (bInRotationMode) return;

	UYUFSNPCDragDropOperation* DragOp = Cast<UYUFSNPCDragDropOperation>(InOperation);
	if (!DragOp || !DragOp->NPCClass) return;

	SpawnPreview(DragOp->NPCClass);

	FVector FloorPos;
	if (GetCursorWorldLocation(FloorPos) && IsValid(ActivePreview))
	{
		ActivePreview->UpdateLocation(FloorPos);
	}

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
	if (bInRotationMode) return false;
	return InOperation && InOperation->IsA<UYUFSNPCDragDropOperation>();
}

bool UYUFSDropZoneWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	if (bInRotationMode) return false;

	GetWorld()->GetTimerManager().ClearTimer(PreviewTickTimer);
	DestroyPreview();

	UYUFSNPCDragDropOperation* DragOp = Cast<UYUFSNPCDragDropOperation>(InOperation);
	if (!DragOp || !DragOp->NPCClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[YUFS|DropZone] 유효하지 않은 DragOp."));
		return false;
	}
	if (SimController && SimController->GetCurrentPhase() != ESimPhase::WaitingToStart)
	{
		UE_LOG(LogTemp, Warning, TEXT("[YUFS|DropZone] 시뮬레이션 시작 전에만 배치 가능합니다."));
		return false;
	}

	FVector FloorPos;
	if (!GetCursorWorldLocation(FloorPos)) return false;

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

	UE_LOG(LogTemp, Log, TEXT("[YUFS] Rotation 모드 — 드래그 영역을 좌우로 드래그해 방향을 설정하세요."));
}

void UYUFSDropZoneWidget::ExitRotationMode(bool bConfirm)
{
	if (RotationWidget)
	{
		RotationWidget->RemoveFromParent();
		RotationWidget = nullptr;
	}

	if (bConfirm)
	{
		// BeginPlay에서 이미 등록됨 — 추가 작업 없음
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

bool UYUFSDropZoneWidget::GetCursorWorldLocation(FVector& OutWorldPos) const
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC) return false;

	UGameViewportClient* ViewportClient = GEngine->GameViewport;
	if (!ViewportClient || !ViewportClient->Viewport) return false;

	FVector2D AbsCursorPos = FSlateApplication::Get().GetCursorPos();
	FVector2D ViewportPos;
	if (!ViewportClient->GetMousePosition(ViewportPos))
	{
		TSharedPtr<SViewport> ViewportWidget = ViewportClient->GetGameViewportWidget();
		if (!ViewportWidget.IsValid()) return false;
		FGeometry VPGeometry = ViewportWidget->GetCachedGeometry();
		ViewportPos = VPGeometry.AbsoluteToLocal(AbsCursorPos);
		ViewportPos *= VPGeometry.Scale;
	}

	FVector RayOrigin, RayDir;
	if (!PC->DeprojectScreenPositionToWorld(ViewportPos.X, ViewportPos.Y, RayOrigin, RayDir))
	{
		return false;
	}

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!NavSys) return false;

	const float VerticalExtent = 150.f;
	float FloorZ = 0.f;
	bool bFoundFloor = false;
	for (float Dist = 200.f; Dist <= 100000.f; Dist += 300.f)
	{
		const FVector TestPoint = RayOrigin + RayDir * Dist;
		FNavLocation NavLoc;
		if (NavSys->ProjectPointToNavigation(TestPoint, NavLoc, FVector(500.f, 500.f, VerticalExtent)))
		{
			FloorZ = NavLoc.Location.Z;
			bFoundFloor = true;
			break;
		}
	}

	if (!bFoundFloor) return false;

	if (FMath::IsNearlyZero(RayDir.Z)) return false;
	const float T = (FloorZ - RayOrigin.Z) / RayDir.Z;
	if (T <= 0.f) return false;

	OutWorldPos = RayOrigin + RayDir * T;
	OutWorldPos.Z = FloorZ;

	FNavLocation FinalNav;
	if (NavSys->ProjectPointToNavigation(OutWorldPos, FinalNav, FVector(200.f, 200.f, 150.f)))
	{
		OutWorldPos = FinalNav.Location;
	}

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
			SpawnLocation.Z += Cap->GetScaledCapsuleHalfHeight();
		}
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

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