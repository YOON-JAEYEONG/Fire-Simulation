#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "YUFSDropZoneWidget.generated.h"

class AYUFSSimulationController;
class AYUFSEvacuationNPC;
class AYUFSNPCPlacementPreview;

UCLASS()
class YUFS_API UYUFSDropZoneWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category="Simulation")
	AYUFSSimulationController* SimController;

	// 고스트 미리보기 액터 클래스 (BP_NPCPlacementPreview를 만들어 연결)
	UPROPERTY(EditDefaultsOnly, Category="Preview")
	TSubclassOf<AYUFSNPCPlacementPreview> PreviewActorClass;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual void NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual bool NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

private:
	// NavMesh 샘플링 — 드래그 진입/드롭 시 정확한 위치 계산
	bool GetCursorWorldLocation(FVector& OutWorldPos) const;

	AYUFSEvacuationNPC* SpawnAndRegisterNPC(TSubclassOf<AYUFSEvacuationNPC> NPCClass, const FVector& FloorLocation);

	void SpawnPreview(TSubclassOf<AYUFSEvacuationNPC> NPCClass);
	void DestroyPreview();

	UPROPERTY() AYUFSNPCPlacementPreview* ActivePreview = nullptr;

	FTimerHandle PreviewTickTimer;
	void TickPreviewUpdate();
};