#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "YUFSNPCPaletteEntry.generated.h"

class AYUFSEvacuationNPC;
class UImage;
class UTextBlock;

UCLASS()
class YUFS_API UYUFSNPCPaletteEntry : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="NPC")
	TSubclassOf<AYUFSEvacuationNPC> NPCClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="NPC")
	FText DisplayName;

	UPROPERTY(meta=(BindWidgetOptional))
	UTextBlock* NPCNameText;

	UPROPERTY(meta=(BindWidgetOptional))
	UImage* NPCIcon;

protected:
	virtual void NativePreConstruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;
};