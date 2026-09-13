#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "YUFSNPCPlacementPreview.generated.h"

class AYUFSEvacuationNPC;
class USkeletalMeshComponent;

UCLASS()
class YUFS_API AYUFSNPCPlacementPreview : public AActor
{
	GENERATED_BODY()

public:
	AYUFSNPCPlacementPreview();

	// NPC 클래스에서 메시/애니메이션 복사 및 고스트 설정
	void InitFromNPCClass(TSubclassOf<AYUFSEvacuationNPC> NPCClass);

	// NavMesh 바닥 위치를 받아 NPC 스폰과 동일한 높이로 이동
	void UpdateLocation(const FVector& FloorLocation);

	// 에디터(Blueprint)에서 반투명 머티리얼 지정
	// 미설정 시 원본 머티리얼의 Opacity 파라미터 변경 시도
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Preview")
	UMaterialInterface* GhostMaterial;

private:
	UPROPERTY() USkeletalMeshComponent* PreviewMesh;
	float CachedCapsuleHalfHeight = 90.f;
};