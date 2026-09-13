#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "YUFSNPCActionWidget.generated.h"

class UButton;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnNPCActionDelete);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnNPCActionCancel);

UCLASS()
class YUFS_API UYUFSNPCActionWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable) FOnNPCActionDelete OnDelete;
	UPROPERTY(BlueprintAssignable) FOnNPCActionCancel OnCancel;

	UPROPERTY(meta=(BindWidget)) UButton* DeleteButton;
	UPROPERTY(meta=(BindWidget)) UButton* CancelButton;

protected:
	virtual void NativeConstruct() override;

private:
	UFUNCTION() void HandleDelete();
	UFUNCTION() void HandleCancel();
};