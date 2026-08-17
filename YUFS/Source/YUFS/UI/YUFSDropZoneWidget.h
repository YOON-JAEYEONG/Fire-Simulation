#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "YUFSDropZoneWidget.generated.h"

class AYUFSSimulationController;
class AYUFSEvacuationNPC;
class AYUFSNPCPlacementPreview;
class UYUFSNPCRotationWidget;

UCLASS()
class YUFS_API UYUFSDropZoneWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category="Simulation")
	AYUFSSimulationController* SimController;

	UPROPERTY(EditDefaultsOnly, Category="Preview")
	TSubclassOf<AYUFSNPCPlacementPreview> PreviewActorClass;

	// 확인/취소 버튼이 있는 위젯 클래스 (WBP_NPCRotation을 만들어 연결)
	UPROPERTY(EditDefaultsOnly, Category="Rotation")
	TSubclassOf<UYUFSNPCRotationWidget> RotationWidgetClass;

protected:
	virtual void NativeConstruct() override;
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
};