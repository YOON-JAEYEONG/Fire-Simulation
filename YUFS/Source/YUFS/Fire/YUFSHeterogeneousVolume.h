// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/HeterogeneousVolumeComponent.h"
#include "GameFramework/Actor.h"
#include "YUFSHeterogeneousVolume.generated.h"

UCLASS()
class YUFS_API AYUFSHeterogeneousVolume : public AActor
{
	GENERATED_BODY()
	
public:	
	AYUFSHeterogeneousVolume();

protected:
	virtual void BeginPlay() override;

public:
	// ── SimulationController에서 호출하는 제어 API ─────────────────────
	// 화재 시뮬레이션 재생 시작 (시뮬레이션 FireActive 단계 진입 시)
	UFUNCTION(BlueprintCallable, Category="Fire")
	void StartFire();

	// 일시정지 (시뮬레이션 Pause 시)
	UFUNCTION(BlueprintCallable, Category="Fire")
	void PauseFire();

	// 재개
	UFUNCTION(BlueprintCallable, Category="Fire")
	void ResumeFire();

	// 완전 리셋 (다음 회차 준비)
	UFUNCTION(BlueprintCallable, Category="Fire")
	void ResetFire();

	// 현재 프레임 번호 반환 (BinaryManager가 읽음)
	UFUNCTION(BlueprintPure, Category="Fire")
	int32 GetFrame() const;

	// 타임라인 관찰 모드에서 특정 화재 프레임으로 즉시 이동합니다.
	UFUNCTION(BlueprintCallable, Category="Fire|Timeline")
	void SetFrame(int32 TargetFrame);

	// 시간(초) → 화재 프레임 변환에 사용할 재생 FPS입니다.
	UFUNCTION(BlueprintPure, Category="Fire|Timeline")
	float GetPlaybackFrameRate() const { return PlaybackFrameRate; }

	// 현재 재생 중인지 여부
	UFUNCTION(BlueprintPure, Category="Fire")
	bool IsPlaying() const;

	/** Existing authored ignition point, in this actor's local coordinates. No fire actor is spawned. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fire|Interaction")
	bool bHasInteractionTarget = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fire|Interaction", meta=(MakeEditWidget="true"))
	FVector InteractionTargetLocal = FVector::ZeroVector;

	UFUNCTION(BlueprintPure, Category="Fire|Interaction")
	bool GetInteractionTarget(FVector& OutWorldLocation) const;
	virtual void Tick(float DeltaSeconds) override;
	bool IsLocalFireBurning() const { return bLocalFireActive && LocalFireStrength > 0.f; }
	void ApplyLocalSuppression(float Amount);

protected:
	UPROPERTY(EditAnywhere, Category="Fire")
	UHeterogeneousVolumeComponent* HeterogeneousVolumeComponent;

public:
	// ── 에디터 설정 ──────────────────────────────────────────────────────
	// 초당 재생할 프레임 수 (기본 8fps)
	UPROPERTY(EditAnywhere, Category="Fire")
	float PlaybackFrameRate = 8.f;

	// 총 프레임 수
	UPROPERTY(EditAnywhere, Category="Fire")
	float TotalFrameCount = 8000.f;

	// 플레이 시작 시 자동으로 재생을 시작할지 여부
	// false로 설정하면 SimulationController의 StartFire() 호출 전까지 정지 상태 유지
	UPROPERTY(EditAnywhere, Category="Fire")
	bool bAutoPlayOnBeginPlay = false;

private:
	void CreateLocalEffects();
	void RefreshLocalEffects();
	UPROPERTY(Transient) TArray<TObjectPtr<class UStaticMeshComponent>> LocalEffectPlanes;
	UPROPERTY(Transient) TArray<TObjectPtr<class UMaterialInstanceDynamic>> LocalEffectMaterials;
	UPROPERTY(Transient) TObjectPtr<class UPointLightComponent> LocalFireLight;
	float LocalFireStrength = 1.f;
	float LocalEffectTime = 0.f;
	bool bLocalFireActive = false;
	bool bLocalEffectsPlaying = false;
};
