#include "UI/YUFSNPCPaletteEntry.h"

#include "Components/Image.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "UI/YUFSNPCDragDropOperation.h"

void UYUFSNPCPaletteEntry::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// Older palette blueprints contain only an image, with no optional text binding.
	if (!NPCNameText && WidgetTree)
	{
		UWidget* OriginalRoot = WidgetTree->RootWidget;
		UOverlay* Overlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass());
		WidgetTree->RootWidget = Overlay;
		if (OriginalRoot) Overlay->AddChildToOverlay(OriginalRoot);
		NPCNameText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		NPCNameText->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
		FSlateFontInfo Font = NPCNameText->GetFont();
		Font.Size = 16;
		NPCNameText->SetFont(Font);
		NPCNameText->SetVisibility(ESlateVisibility::HitTestInvisible);
		UOverlaySlot* LabelSlot = Overlay->AddChildToOverlay(NPCNameText);
		LabelSlot->SetHorizontalAlignment(HAlign_Center);
		LabelSlot->SetVerticalAlignment(VAlign_Center);
	}
}

void UYUFSNPCPaletteEntry::NativePreConstruct()
{
	Super::NativePreConstruct();
	SetToolTipText(NSLOCTEXT("YUFS", "PlacementHelp", "시작 전에 NPC를 건물 바닥으로 드래그하고 방향을 확정하세요."));

	if (NPCNameText)
	{
		NPCNameText->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
		NPCNameText->SetVisibility(ESlateVisibility::HitTestInvisible);
		NPCNameText->SetText(DisplayName.IsEmpty() ? NSLOCTEXT("YUFS", "PlaceNPC", "NPC 배치") : DisplayName);
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
