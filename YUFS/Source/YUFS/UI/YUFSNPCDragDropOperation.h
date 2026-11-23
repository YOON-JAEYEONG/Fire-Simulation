#pragma once

#include "CoreMinimal.h"
#include "Blueprint/DragDropOperation.h"
#include "YUFSNPCDragDropOperation.generated.h"

class AYUFSEvacuationNPC;
class UYUFSNPCPaletteWidget;

UCLASS()
class YUFS_API UYUFSNPCDragDropOperation : public UDragDropOperation
{
	GENERATED_BODY()

public:
	int32 PlacementCount = 1;
	TWeakObjectPtr<UYUFSNPCPaletteWidget> SourcePalette;

	UPROPERTY(BlueprintReadWrite, Category="DragDrop")
	TSubclassOf<AYUFSEvacuationNPC> NPCClass;

	UPROPERTY(BlueprintReadWrite, Category="DragDrop")
	FText DisplayName;
};
