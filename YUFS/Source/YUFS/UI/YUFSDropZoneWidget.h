#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "YUFSDropZoneWidget.generated.h"

class AYUFSSimulationController;
class AYUFSEvacuationNPC;
class AYUFSNPCPlacementPreview;
class UYUFSNPCRotationWidget;
class UYUFSNPCActionWidget;

UCLASS()
class YUFS_API UYUFSDropZoneWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category="Simulation")
	AYUFSSimulationController* SimController;

	UPROPERTY(EditDefaultsOnly, Category="Preview")
	TSubclassOf<AYUFSNPCPlacementPreview> PreviewActorClass;

	UPROPERTY(EditDefaultsOnly, Category="Rotation")
	TSubclassOf<UYUFSNPCRotationWidget> RotationWidgetClass;

	// 삭제/취소 버튼 위젯 클래스 (WBP_NPCAction을 만들어 연결)
	UPROPERTY(EditDefaultsOnly, Category="NPCAction")
	TSubclassOf<UYUFSNPCActionWidget> NPCActionWidgetClass;

protected:
	virtual void NativeConstruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragEnter(const FGeometry&, const FDragDropEvent&, UDragDropOperation*) override;
	virtual void NativeOnDragLeave(const FDragDropEvent&, UDragDropOperation*) override;
	virtual bool NativeOnDragOver(const FGeometry&, const FDragDropEvent&, UDragDropOperation*) override;
	virtual bool NativeOnDrop(const FGeometry&, const FDragDropEvent&, UDragDropOperation*) override;

private:
	bool GetCursorWorldLocation(FVector& OutWorldPos) const;

	// ── 드래그 미리보기 ──────────────────────────────────────────────
	void SpawnPreview(TSubclassOf<AYUFSEvacuationNPC> NPCClass);
	void DestroyPreview();

	UPROPERTY() AYUFSNPCPlacementPreview* ActivePreview = nullptr;
	FTimerHandle PreviewTickTimer;
	void TickPreviewUpdate();

	// ── Rotation 모드 ─────────────────────────────────────────────────
	void EnterRotationMode(AYUFSEvacuationNPC* NPC);
	void ExitRotationMode(bool bConfirm);

	UFUNCTION() void OnRotationConfirmed();
	UFUNCTION() void OnRotationCancelled();
	UFUNCTION() void OnRotationDelta(float DeltaYaw);

	AYUFSEvacuationNPC* SpawnNPC(TSubclassOf<AYUFSEvacuationNPC> NPCClass, const FVector& FloorLocation);

	UPROPERTY() AYUFSEvacuationNPC* PendingNPC = nullptr;
	UPROPERTY() UYUFSNPCRotationWidget* RotationWidget = nullptr;
	bool bInRotationMode = false;

	// ── NPC 액션 (클릭 선택 → 삭제/취소) ────────────────────────────
	bool TryGetNPCUnderCursor(AYUFSEvacuationNPC*& OutNPC) const;
	void ShowNPCActionWidget(AYUFSEvacuationNPC* NPC);
	void HideNPCActionWidget();
	void TryDetectNPCClick();       // 타이머 폴링 방식 (위젯 히트 미스 대비)

	UFUNCTION() void OnNPCDeleteClicked();
	UFUNCTION() void OnNPCCancelClicked();

	UPROPERTY() AYUFSEvacuationNPC* SelectedNPC = nullptr;
	UPROPERTY() UYUFSNPCActionWidget* NPCActionWidget = nullptr;
	bool bShowingNPCAction = false;

	FTimerHandle ClickDetectionTimer;
	bool bWasLeftMouseDown = false;
};