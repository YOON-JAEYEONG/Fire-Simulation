#include "UI/YUFSDropZoneWidget.h"

#include "Application/SlateApplicationBase.h"
#include "Widgets/SViewport.h"
#include "Components/CapsuleComponent.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "NavigationSystem.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "Simulation/YUFSSimulationController.h"
#include "UI/YUFSNPCDragDropOperation.h"
#include "UI/YUFSNPCPlacementPreview.h"

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

// ── 타이머로 ~60fps 위치 갱신 ─────────────────────────────────────────────
void UYUFSDropZoneWidget::TickPreviewUpdate()
{
	if (!IsValid(ActivePreview)) return;

	// 매 프레임 GetCursorWorldLocation으로 다층 건물 대응
	FVector FloorPos;
	if (GetCursorWorldLocation(FloorPos))
	{
		ActivePreview->UpdateLocation(FloorPos);
	}
}

// ── 드래그 진입: 고스트 NPC 생성 + 초기 위치 설정 ────────────────────────
void UYUFSDropZoneWidget::NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragEnter(InGeometry, InDragDropEvent, InOperation);

	UYUFSNPCDragDropOperation* DragOp = Cast<UYUFSNPCDragDropOperation>(InOperation);
	if (!DragOp || !DragOp->NPCClass) return;

	SpawnPreview(DragOp->NPCClass);

	// 초기 위치 설정
	FVector FloorPos;
	if (GetCursorWorldLocation(FloorPos) && IsValid(ActivePreview))
	{
		ActivePreview->UpdateLocation(FloorPos);
	}

	// ~60fps 타이머로 위치 갱신 시작
	GetWorld()->GetTimerManager().SetTimer(
		PreviewTickTimer,
		this, &UYUFSDropZoneWidget::TickPreviewUpdate,
		0.016f, true);
}

// ── 드래그 이탈: 고스트 NPC 제거 + 캐시 초기화 ───────────────────────────
void UYUFSDropZoneWidget::NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragLeave(InDragDropEvent, InOperation);
	GetWorld()->GetTimerManager().ClearTimer(PreviewTickTimer);
	DestroyPreview();
}

// ── 드래그 중: 드롭 허용 여부만 반환 (위치 갱신은 타이머가 담당) ──────────
bool UYUFSDropZoneWidget::NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	return InOperation && InOperation->IsA<UYUFSNPCDragDropOperation>();
}

// ── 드롭: 고스트 제거 후 실제 NPC 스폰 ───────────────────────────────────
bool UYUFSDropZoneWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	GetWorld()->GetTimerManager().ClearTimer(PreviewTickTimer);
	DestroyPreview();

	UYUFSNPCDragDropOperation* DragOp = Cast<UYUFSNPCDragDropOperation>(InOperation);
	if (!DragOp)
	{
		UE_LOG(LogTemp, Warning, TEXT("[YUFS|DropZone] DragDropOperation 캐스트 실패."));
		return false;
	}
	if (!DragOp->NPCClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[YUFS|DropZone] NPCClass가 null — PaletteEntry에서 NPCClass를 설정했는지 확인하세요."));
		return false;
	}
	if (SimController && SimController->GetCurrentPhase() != ESimPhase::WaitingToStart)
	{
		UE_LOG(LogTemp, Warning, TEXT("[YUFS|DropZone] 시뮬레이션 시작 전에만 배치 가능합니다."));
		return false;
	}

	FVector FloorPos;
	if (!GetCursorWorldLocation(FloorPos))
	{
		return false;
	}

	AYUFSEvacuationNPC* SpawnedNPC = SpawnAndRegisterNPC(DragOp->NPCClass, FloorPos);
	return IsValid(SpawnedNPC);
}

// ── 위치 계산 ─────────────────────────────────────────────────────────────
bool UYUFSDropZoneWidget::GetCursorWorldLocation(FVector& OutWorldPos) const
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC) return false;

	// FSlateApplication으로 절대 스크린 좌표를 가져와 뷰포트 픽셀 좌표로 변환
	// GetMousePosition()은 DPI 스케일 적용 여부가 플랫폼마다 달라 Slate 방식을 사용
	UGameViewportClient* ViewportClient = GEngine->GameViewport;
	if (!ViewportClient || !ViewportClient->Viewport) return false;

	FVector2D AbsCursorPos = FSlateApplication::Get().GetCursorPos();
	FVector2D ViewportPos;
	if (!ViewportClient->GetMousePosition(ViewportPos))
	{
		// 폴백: GetMousePosition 실패 시 Slate 절대 위치 → 뷰포트 로컬 변환
		TSharedPtr<SViewport> ViewportWidget = ViewportClient->GetGameViewportWidget();
		if (!ViewportWidget.IsValid()) return false;
		FGeometry VPGeometry = ViewportWidget->GetCachedGeometry();
		ViewportPos = VPGeometry.AbsoluteToLocal(AbsCursorPos);
		// 로컬 좌표(논리 픽셀) → 실제 픽셀 변환
		const float DPIScale = VPGeometry.Scale;
		ViewportPos *= DPIScale;
	}

	FVector RayOrigin, RayDir;
	if (!PC->DeprojectScreenPositionToWorld(ViewportPos.X, ViewportPos.Y, RayOrigin, RayDir))
	{
		UE_LOG(LogTemp, Warning, TEXT("[YUFS|DropZone] Deproject 실패 (ViewportPos=%.0f,%.0f)"), ViewportPos.X, ViewportPos.Y);
		return false;
	}

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!NavSys) return false;

	// 레이를 따라 샘플링 — 수직 범위를 좁게 유지해서 커서가 가리키는 층만 탐지
	// (수직 범위가 넓으면 2층에서도 1층 NavMesh가 잡힘)
	const float VerticalExtent = 150.f;  // 층간 높이의 절반보다 작게

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

	if (!bFoundFloor)
	{
		return false;
	}

	// 찾은 층 Z에서 레이-평면 교차 → 정확한 X,Y
	if (FMath::IsNearlyZero(RayDir.Z)) return false;
	const float T = (FloorZ - RayOrigin.Z) / RayDir.Z;
	if (T <= 0.f) return false;

	OutWorldPos = RayOrigin + RayDir * T;
	OutWorldPos.Z = FloorZ;

	// 최종 NavMesh 스냅
	FNavLocation FinalNav;
	if (NavSys->ProjectPointToNavigation(OutWorldPos, FinalNav, FVector(200.f, 200.f, 150.f)))
	{
		OutWorldPos = FinalNav.Location;
	}

	return true;
}

// ── 실제 NPC 스폰 ─────────────────────────────────────────────────────────
AYUFSEvacuationNPC* UYUFSDropZoneWidget::SpawnAndRegisterNPC(TSubclassOf<AYUFSEvacuationNPC> NPCClass, const FVector& FloorLocation)
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

	AYUFSEvacuationNPC* NPC = GetWorld()->SpawnActor<AYUFSEvacuationNPC>(NPCClass, SpawnLocation, FRotator::ZeroRotator, Params);
	if (IsValid(NPC))
	{
		if (SimController) SimController->RegisterNPC(NPC);
		UE_LOG(LogTemp, Log, TEXT("[YUFS] NPC '%s' 배치 완료 → %s"), *NPC->GetName(), *NPC->GetActorLocation().ToString());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[YUFS|DropZone] SpawnActor 실패 — %s @ %s"), *NPCClass->GetName(), *SpawnLocation.ToString());
	}

	return NPC;
}

// ── 고스트 프리뷰 관리 ────────────────────────────────────────────────────
void UYUFSDropZoneWidget::SpawnPreview(TSubclassOf<AYUFSEvacuationNPC> NPCClass)
{
	DestroyPreview();

	TSubclassOf<AYUFSNPCPlacementPreview> SpawnClass =
		PreviewActorClass ? PreviewActorClass : TSubclassOf<AYUFSNPCPlacementPreview>(AYUFSNPCPlacementPreview::StaticClass());

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