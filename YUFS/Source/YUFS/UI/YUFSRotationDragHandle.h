#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "YUFSRotationDragHandle.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRotationHandleDelta, float, DeltaX);

// 드래그 전용 위젯 — 버튼과 분리된 영역에 배치해 이벤트 충돌 방지
UCLASS()
class YUFS_API UYUFSRotationDragHandle : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FOnRotationHandleDelta OnDragDelta;

protected:
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FCursorReply NativeOnCursorQuery(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	bool bIsDragging = false;
	float LastMouseX = 0.f;
};