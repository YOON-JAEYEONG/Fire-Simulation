#include "UI/YUFSRotationDragHandle.h"

FReply UYUFSRotationDragHandle::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bIsDragging = true;
		LastMouseX = InMouseEvent.GetScreenSpacePosition().X;
		// 마우스 캡처로 위젯 밖으로 나가도 드래그 지속
		return FReply::Handled().CaptureMouse(TakeWidget());
	}
	return FReply::Unhandled();
}

FReply UYUFSRotationDragHandle::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bIsDragging)
	{
		const float CurrentX = InMouseEvent.GetScreenSpacePosition().X;
		const float DeltaX   = CurrentX - LastMouseX;
		LastMouseX = CurrentX;

		if (!FMath::IsNearlyZero(DeltaX))
		{
			OnDragDelta.Broadcast(DeltaX);
		}
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply UYUFSRotationDragHandle::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bIsDragging)
	{
		bIsDragging = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FCursorReply UYUFSRotationDragHandle::NativeOnCursorQuery(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// 드래그 중에는 좌우 화살표 커서로 변경
	return bIsDragging
		? FCursorReply::Cursor(EMouseCursor::ResizeLeftRight)
		: FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
}