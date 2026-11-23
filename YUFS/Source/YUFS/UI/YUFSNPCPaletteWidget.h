#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "YUFSNPCPaletteWidget.generated.h"

class AYUFSEvacuationNPC;
class UComboBoxString;
class UScrollBox;
class UTextBlock;
class UYUFSNPCPaletteEntry;

USTRUCT(BlueprintType)
struct FYUFSNPCPaletteItem
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	TSubclassOf<AYUFSEvacuationNPC> NPCClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	FText DisplayName;
};

UCLASS()
class YUFS_API UYUFSNPCPaletteWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	int32 GetPlacementCount() const;
	void SetPlacementStatus(const FText& Status);

	// 에디터에서 NPC 종류와 이름을 설정
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Palette")
	TArray<FYUFSNPCPaletteItem> AvailableNPCs;

	// 각 항목으로 사용할 위젯 클래스 (WBP_NPCPaletteEntry를 연결)
	UPROPERTY(EditDefaultsOnly, Category="Palette")
	TSubclassOf<UYUFSNPCPaletteEntry> EntryWidgetClass;

	UPROPERTY(meta=(BindWidget))
	UScrollBox* NPCListBox;

protected:
	virtual void NativeConstruct() override;

private:
	void PopulateList();

	UPROPERTY(Transient) TObjectPtr<UComboBoxString> PlacementCountPicker;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PlacementStatus;
};
