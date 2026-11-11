#include "UI/YUFSNPCActionWidget.h"

#include "Components/Button.h"

void UYUFSNPCActionWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (DeleteButton) DeleteButton->OnClicked.AddDynamic(this, &UYUFSNPCActionWidget::HandleDelete);
	if (CancelButton) CancelButton->OnClicked.AddDynamic(this, &UYUFSNPCActionWidget::HandleCancel);
}

void UYUFSNPCActionWidget::HandleDelete()
{
	OnDelete.Broadcast();
}

void UYUFSNPCActionWidget::HandleCancel()
{
	OnCancel.Broadcast();
}