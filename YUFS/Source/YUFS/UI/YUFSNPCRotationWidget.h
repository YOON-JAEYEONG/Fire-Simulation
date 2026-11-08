#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "YUFSNPCRotationWidget.generated.h"

class UButton;
class UTextBlock;
class UYUFSRotationDragHandle;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnNPCRotationConfirmed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnNPCRotationCancelled);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNPCRotationDelta, float, DeltaYaw);

UCLASS()
class YUFS_API UYUFSNPCRotationWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable) FOnNPCRotationConfirmed OnConfirmed;
	UPROPERTY(BlueprintAssignable) FOnNPCRotationCancelled OnCancelled;
	UPROPERTY(BlueprintAssignable) FOnNPCRotationDelta OnRotationDelta;

	// WBP에서 BindWidget으로 연결
	UPROPERTY(meta=(BindWidget)) UButton* ConfirmButton;
	UPROPERTY(meta=(BindWidget)) UButton* CancelButton;
	UPROPERTY(meta=(BindWidget)) UYUFSRotationDragHandle* DragHandle;

	UPROPERTY(meta=(BindWidgetOptional)) UTextBlock* RotationAngleText;

	// 드래그 민감도 (도/픽셀)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Rotation", meta=(ClampMin="0.1"))
	float RotationSensitivity = 0.5f;

	void SetAngleDisplay(float Yaw);

protected:
	virtual void NativeConstruct() override;

private:
	UFUNCTION() void HandleConfirm();
	UFUNCTION() void HandleCancel();
	UFUNCTION() void HandleDragDelta(float DeltaX);
};