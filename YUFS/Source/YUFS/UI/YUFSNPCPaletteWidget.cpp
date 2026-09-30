#include "UI/YUFSNPCPaletteWidget.h"
#include "NPC/YUFSEvacuationNPC.h"

#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "UI/YUFSNPCPaletteEntry.h"

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
		if (Entry->NPCNameText) Entry->NPCNameText->SetText(Entry->DisplayName);

		NPCListBox->AddChild(Entry);
	}
}
