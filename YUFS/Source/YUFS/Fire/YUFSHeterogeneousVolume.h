// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/HeterogeneousVolumeComponent.h"
#include "GameFramework/Actor.h"
#include "YUFSHeterogeneousVolume.generated.h"

// 화재 시나리오 하나를 정의하는 데이터 구조체
// 에디터에서 배열로 설정하고, 시뮬레이션 시작 시 SetActiveScenario()로 전환
USTRUCT(BlueprintType)
struct FFireScenarioConfig
{
	GENERATED_BODY()

	// Content/ 기준 상대 경로 (예: "Fires/ScenarioA/smoke_data.bin")
	UPROPERTY(EditAnywhere, Category="Fire")
	FString BinaryDataPath;

	// SparseVolume을 참조하는 머티리얼 인스턴스
	UPROPERTY(EditAnywhere, Category="Fire")
	TSoftObjectPtr<UMaterialInterface> SparseMaterial;

	// 이 화재 데이터의 월드 좌표계 원점 (복셀 인덱스 0,0,0에 해당하는 월드 위치, BinaryManager용)
	UPROPERTY(EditAnywhere, Category="Fire")
	FVector WorldOrigin = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, Category="Fire")
	float TotalFrameCount = 8000.f;

	UPROPERTY(EditAnywhere, Category="Fire")
	float PlaybackFrameRate = 8.f;
};

UCLASS()
class YUFS_API AYUFSHeterogeneousVolume : public AActor
{
	GENERATED_BODY()

public:
	AYUFSHeterogeneousVolume();

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

public:
	// ── SimulationController에서 호출하는 제어 API ─────────────────────
	UFUNCTION(BlueprintCallable, Category="Fire")
	void StartFire();

	UFUNCTION(BlueprintCallable, Category="Fire")
	void PauseFire();

	UFUNCTION(BlueprintCallable, Category="Fire")
	void ResumeFire();

	UFUNCTION(BlueprintCallable, Category="Fire")
	void ResetFire();

	// 활성 시나리오 전환: 머티리얼 교체 + 프레임 파라미터 설정
	// SimulationController가 FireActive 진입 직전에 호출
	UFUNCTION(BlueprintCallable, Category="Fire")
	void SetActiveScenario(int32 Index);

	// 현재 프레임 번호 반환 (BinaryManager가 읽음)
	UFUNCTION(BlueprintPure, Category="Fire")
	int32 GetFrame() const;

	UFUNCTION(BlueprintPure, Category="Fire")
	bool IsPlaying() const;

	// BinaryManager가 InitializeForScenario() 시 호출해 경로·원점을 가져감
	UFUNCTION(BlueprintPure, Category="Fire")
	FString GetActiveScenarioBinaryPath() const;

	UFUNCTION(BlueprintPure, Category="Fire")
	FVector GetActiveScenarioWorldOrigin() const;

	// 현재 활성 시나리오의 VolumeComponent 반환
	UFUNCTION(BlueprintPure, Category="Fire")
	UHeterogeneousVolumeComponent* GetActiveVolumeComponent() const;

public:
	// ── 에디터 설정 ──────────────────────────────────────────────────────
	// 화재 시나리오 목록. 항목을 추가하면 OnConstruction에서 VolumeComponent가 자동 생성됨
	UPROPERTY(EditAnywhere, Category="Fire")
	TArray<FFireScenarioConfig> FireScenarios;

	// 에디터에서 지정하는 기본 시나리오 인덱스 (BeginPlay 시 적용)
	UPROPERTY(EditAnywhere, Category="Fire")
	int32 ActiveScenarioIndex = 0;

	// true이면 BeginPlay 즉시 재생 (에디터 테스트용)
	UPROPERTY(EditAnywhere, Category="Fire")
	bool bAutoPlayOnBeginPlay = false;

	// FireScenarios[i]에 대응하는 VolumeComponent. OnConstruction에서 자동 동기화됨
	// Viewport에서 위치 직접 조정 가능
	UPROPERTY(VisibleAnywhere, Category="Fire")
	TArray<TObjectPtr<UHeterogeneousVolumeComponent>> VolumeComponents;
};
