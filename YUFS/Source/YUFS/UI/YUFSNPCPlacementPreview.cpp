#include "UI/YUFSNPCPlacementPreview.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NPC/YUFSEvacuationNPC.h"

AYUFSNPCPlacementPreview::AYUFSNPCPlacementPreview()
{
	PrimaryActorTick.bCanEverTick = false;

	PreviewMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("PreviewMesh"));
	SetRootComponent(PreviewMesh);
	PreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewMesh->SetCastShadow(false);
}

void AYUFSNPCPlacementPreview::InitFromNPCClass(TSubclassOf<AYUFSEvacuationNPC> NPCClass)
{
	if (!NPCClass) return;

	AYUFSEvacuationNPC* CDO = Cast<AYUFSEvacuationNPC>(NPCClass->GetDefaultObject());
	if (!CDO) return;

	if (const UCapsuleComponent* Cap = CDO->GetCapsuleComponent())
	{
		CachedCapsuleHalfHeight = Cap->GetScaledCapsuleHalfHeight();
	}

	USkeletalMeshComponent* SrcMesh = CDO->GetMesh();
	if (!SrcMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("[YUFS|Preview] CDO의 GetMesh()가 null입니다."));
		return;
	}

	USkeletalMesh* MeshAsset = SrcMesh->GetSkeletalMeshAsset();
	if (!MeshAsset)
	{
		UE_LOG(LogTemp, Warning, TEXT("[YUFS|Preview] GetSkeletalMeshAsset()가 null — Blueprint에서 메시가 설정되지 않았습니다."));
	}

	PreviewMesh->SetSkeletalMesh(MeshAsset);
	PreviewMesh->SetAnimInstanceClass(SrcMesh->GetAnimClass());
	// PreviewMesh가 루트이므로 RelativeLocation은 월드 위치가 됨 — 위치는 UpdateLocation이 담당
	// 회전만 CDO에서 복사해 캐릭터 방향을 맞춤
	PreviewMesh->SetRelativeRotation(SrcMesh->GetRelativeRotation());
	PreviewMesh->SetRelativeLocation(FVector::ZeroVector);

	if (GhostMaterial)
	{
		// Blueprint에서 지정한 반투명 머티리얼로 전체 슬롯 오버라이드
		for (int32 i = 0; i < PreviewMesh->GetNumMaterials(); i++)
		{
			PreviewMesh->SetMaterial(i, GhostMaterial);
		}
	}
	else
	{
		// 원본 머티리얼에서 DMI 생성 후 Opacity 파라미터 설정 시도
		// 머티리얼이 Opacity 파라미터를 지원할 때만 실제로 적용됨
		for (int32 i = 0; i < PreviewMesh->GetNumMaterials(); i++)
		{
			if (UMaterialInterface* Mat = PreviewMesh->GetMaterial(i))
			{
				UMaterialInstanceDynamic* DMI = UMaterialInstanceDynamic::Create(Mat, this);
				DMI->SetScalarParameterValue(TEXT("Opacity"), 0.4f);
				PreviewMesh->SetMaterial(i, DMI);
			}
		}
	}
}

void AYUFSNPCPlacementPreview::UpdateLocation(const FVector& FloorLocation)
{
	SetActorLocation(FloorLocation);
}