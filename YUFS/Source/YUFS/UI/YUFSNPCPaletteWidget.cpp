#include "UI/YUFSNPCPaletteWidget.h"

#include "Components/ScrollBox.h"
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
		Entry->DisplayName = Item.DisplayName;

		NPCListBox->AddChild(Entry);
	}
}