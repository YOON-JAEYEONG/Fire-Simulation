#include "UI/YUFSNPCRotationWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "UI/YUFSRotationDragHandle.h"

void UYUFSNPCRotationWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (ConfirmButton) ConfirmButton->OnClicked.AddDynamic(this, &UYUFSNPCRotationWidget::HandleConfirm);
	if (CancelButton)  CancelButton->OnClicked.AddDynamic(this, &UYUFSNPCRotationWidget::HandleCancel);

	// 드래그 핸들은 별도 위젯 — 버튼과 이벤트 충돌 없음
	if (DragHandle) DragHandle->OnDragDelta.AddDynamic(this, &UYUFSNPCRotationWidget::HandleDragDelta);
}

void UYUFSNPCRotationWidget::HandleDragDelta(float DeltaX)
{
	OnRotationDelta.Broadcast(DeltaX * RotationSensitivity);
}

void UYUFSNPCRotationWidget::SetAngleDisplay(float Yaw)
{
	if (RotationAngleText)
	{
		const float Normalized = FMath::Fmod(Yaw + 360.f, 360.f);
		RotationAngleText->SetText(FText::FromString(FString::Printf(TEXT("%.0f°"), Normalized)));
	}
}

void UYUFSNPCRotationWidget::HandleConfirm()
{
	OnConfirmed.Broadcast();
}

void UYUFSNPCRotationWidget::HandleCancel()
{
	OnCancelled.Broadcast();
}