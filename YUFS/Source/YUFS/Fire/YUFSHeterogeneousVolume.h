#pragma once
#include "CoreMinimal.h"
#include "Components/HeterogeneousVolumeComponent.h"
#include "GameFramework/Actor.h"
#include "Fire/YUFSFdsIgnitionMetadata.h"
#include "YUFSHeterogeneousVolume.generated.h"

/** Original FDS playback only. NPC actions cannot change its recorded fire field. */
UCLASS()
class YUFS_API AYUFSHeterogeneousVolume : public AActor
{
	GENERATED_BODY()
public:
	AYUFSHeterogeneousVolume();
	UFUNCTION(BlueprintCallable, Category="Fire") void StartFire();
	UFUNCTION(BlueprintCallable, Category="Fire") void PauseFire();
	UFUNCTION(BlueprintCallable, Category="Fire") void ResumeFire();
	UFUNCTION(BlueprintCallable, Category="Fire") void ResetFire();

	// 콤보박스로 화재를 전환할 때 SimulationController가 호출합니다. 레벨을 리로드하지 않고
	// 이 볼륨의 vdb 머티리얼(Sparse Volume Texture 인스턴스)만 교체합니다.
	UFUNCTION(BlueprintCallable, Category="Fire")
	void SetFireMaterial(class UMaterialInterface* NewMaterial);

	// 비어있지 않은 경로가 오면 이 화재 전용 발화 지점 메타데이터(.json)를 다시 로드합니다.
	// 비어 있으면(FYUFSFireOption에 설정하지 않은 경우) 기존 값을 그대로 유지합니다.
	UFUNCTION(BlueprintCallable, Category="Fire")
	void SetIgnitionMetadataFile(const FString& NewMetadataFile);

	UFUNCTION(BlueprintPure, Category="Fire") int32 GetFrame() const;
	UFUNCTION(BlueprintCallable, Category="Fire|Timeline") void SetFrame(int32 TargetFrame);
	UFUNCTION(BlueprintPure, Category="Fire|Timeline") float GetPlaybackFrameRate() const { return PlaybackFrameRate; }
	UFUNCTION(BlueprintPure, Category="Fire") bool IsPlaying() const;
	UFUNCTION(BlueprintPure, Category="Fire|Interaction") bool GetInteractionTarget(FVector& OutWorldLocation) const;
	bool IsFdsFireActive() const;
	bool HasFdsIgnitionMetadata() const { return bMetadataLoaded; }
	const FString& GetFdsTargetDiagnostic() const { return MetadataDiagnostic; }

	// Serialization compatibility only. NEVER use these unverified coordinates as a fallback.
	UPROPERTY(meta=(DeprecatedProperty, DeprecationMessage="Use validated FDS ignition metadata."))
	bool bHasInteractionTarget = false;
	UPROPERTY(meta=(DeprecatedProperty, DeprecationMessage="Unverified local fire coordinates are not used."))
	FVector InteractionTargetLocal = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, Category="Fire") float PlaybackFrameRate = 8.f;
	UPROPERTY(EditAnywhere, Category="Fire") float TotalFrameCount = 8000.f;
	UPROPERTY(EditAnywhere, Category="Fire") bool bAutoPlayOnBeginPlay = false;
	UPROPERTY(EditAnywhere, Category="Fire|FDS") FString IgnitionMetadataFile;
protected:
	virtual void BeginPlay() override;
	UPROPERTY(EditAnywhere, Category="Fire") UHeterogeneousVolumeComponent* HeterogeneousVolumeComponent;
private:
	FYUFSFdsIgnitionMetadata IgnitionMetadata;
	FString MetadataDiagnostic;
	bool bMetadataLoaded = false;
	bool bFireActive = false;
};
