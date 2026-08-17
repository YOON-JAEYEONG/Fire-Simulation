#include "UI/YUFSNPCPaletteEntry.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "UI/YUFSNPCDragDropOperation.h"

void UYUFSNPCPaletteEntry::NativePreConstruct()
{
	Super::NativePreConstruct();

	if (NPCNameText)
	{
		NPCNameText->SetText(DisplayName);
	}
}

FReply UYUFSNPCPaletteEntry::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
	}
	return FReply::Unhandled();
}

void UYUFSNPCPaletteEntry::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
	UYUFSNPCDragDropOperation* DragOp = NewObject<UYUFSNPCDragDropOperation>(this);
	DragOp->NPCClass   = NPCClass;
	DragOp->DisplayName = DisplayName;
	DragOp->DefaultDragVisual = nullptr;
	DragOp->Pivot = EDragPivot::MouseDown;

	OutOperation = DragOp;
}