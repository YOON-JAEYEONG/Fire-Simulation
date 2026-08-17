#pragma once

#include "CoreMinimal.h"
#include "Blueprint/DragDropOperation.h"
#include "YUFSNPCDragDropOperation.generated.h"

class AYUFSEvacuationNPC;

UCLASS()
class YUFS_API UYUFSNPCDragDropOperation : public UDragDropOperation
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category="DragDrop")
	TSubclassOf<AYUFSEvacuationNPC> NPCClass;

	UPROPERTY(BlueprintReadWrite, Category="DragDrop")
	FText DisplayName;
};