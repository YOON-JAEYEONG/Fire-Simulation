#include "UI/YUFSNPCPaletteWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ComboBoxString.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/TextBlock.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "UI/YUFSNPCPaletteEntry.h"

namespace
{
	constexpr int32 PlacementCounts[] = {1, 5, 10};
}

void UYUFSNPCPaletteWidget::NativeConstruct()
{
	Super::NativeConstruct();
	PopulateList();
}

void UYUFSNPCPaletteWidget::PopulateList()
{
	if (!NPCListBox || !EntryWidgetClass)
	{
		return;
	}

	NPCListBox->ClearChildren();

	PlacementCountPicker = WidgetTree->ConstructWidget<UComboBoxString>();
	PlacementCountPicker->SetToolTipText(NSLOCTEXT("YUFS", "NPCCountHelp", "배치 인원을 선택한 뒤 NPC 항목을 바닥으로 드래그하세요."));
	for (const int32 Count : PlacementCounts)
	{
		PlacementCountPicker->AddOption(FText::Format(
			NSLOCTEXT("YUFS", "NPCCountOption", "NPC {0}명 배치"), Count).ToString());
	}
	PlacementCountPicker->SetSelectedIndex(0);
	// 콤보박스와 아래 NPC 항목이 붙어 겹쳐 보이지 않도록 아래쪽 여백을 둡니다.
	if (UScrollBoxSlot* PickerSlot = Cast<UScrollBoxSlot>(NPCListBox->AddChild(PlacementCountPicker)))
	{
		PickerSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	}

	for (const FYUFSNPCPaletteItem& Item : AvailableNPCs)
	{
		if (!Item.NPCClass)
		{
			continue;
		}

		UYUFSNPCPaletteEntry* Entry = CreateWidget<UYUFSNPCPaletteEntry>(this, EntryWidgetClass);
		if (!Entry)
		{
			continue;
		}

		Entry->NPCClass   = Item.NPCClass;
		Entry->DisplayName = Item.DisplayName.IsEmpty() ? NSLOCTEXT("YUFS", "PlaceNPC", "NPC 배치") : Item.DisplayName;
		Entry->SourcePalette = this;
		if (Entry->NPCNameText) Entry->NPCNameText->SetText(Entry->DisplayName);

		NPCListBox->AddChild(Entry);
	}

	PlacementStatus = WidgetTree->ConstructWidget<UTextBlock>();
	PlacementStatus->SetAutoWrapText(true);
	PlacementStatus->SetVisibility(ESlateVisibility::Collapsed);
	NPCListBox->AddChild(PlacementStatus);
}

int32 UYUFSNPCPaletteWidget::GetPlacementCount() const
{
	const int32 Index = PlacementCountPicker ? PlacementCountPicker->GetSelectedIndex() : INDEX_NONE;
	return Index >= 0 && Index < UE_ARRAY_COUNT(PlacementCounts) ? PlacementCounts[Index] : 1;
}

void UYUFSNPCPaletteWidget::SetPlacementStatus(const FText& Status)
{
	if (PlacementStatus)
	{
		PlacementStatus->SetText(Status);
		PlacementStatus->SetVisibility(Status.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}
