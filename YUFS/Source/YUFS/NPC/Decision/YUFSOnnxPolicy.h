// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Core/YUFSDecisionPolicy.h"
#include "Core/YUFSObservation.h"
#include "NNERuntimeCPU.h"
#include "NNERuntimeGPU.h"
#include "YUFSRuleBasedPolicy.h"

struct FYUFSSharedOnnxModelState;

class YUFS_API FYUFSOnnxPolicy : public IYUFSDecisionPolicy
{
public:
	FYUFSOnnxPolicy(
		const FString& InModelPath = FString(),
		const FString& InRuntimeName = TEXT("NNERuntimeORTCpu"));

	virtual EYUFSAction SelectAction(const FYUFSNPCObservation& Obs) override;
	virtual bool IsDataCollectionMode() const override { return bDataCollectionMode; }
	virtual void LoadModel(const FString& Path) override;

	// 데이터 수집 모드: ONNX 모델 대신 룰베이스 폴백을 강제해 학습 데이터를 수집
	void SetDataCollectionMode(bool bEnable) { bDataCollectionMode = bEnable; }

private:
	bool EnsureModelLoaded();
	bool LoadCpuModel(FYUFSSharedOnnxModelState& State, const FString& ResolvedPath, const TArray64<uint8>& ModelBytes);
	bool LoadGpuModel(FYUFSSharedOnnxModelState& State, const FString& ResolvedPath, const TArray64<uint8>& ModelBytes);
	bool ConfigureCpuModelInstance(FYUFSSharedOnnxModelState& State);
	bool ConfigureGpuModelInstance(FYUFSSharedOnnxModelState& State);
	bool RunCpuInference();
	bool RunGpuInference();
	static FString FindLatestOnnxModelPath();
	static FString ResolveModelPath(const FString& InPath);
	EYUFSAction SelectActionFromLogits(const TArray<float>& Logits) const;
	void ResetLoadedModelState();

	// ── 공통 헬퍼 (CPU/GPU Configure·Run 중복 제거) ───────────────────────
	bool ParseInputShape(
		TConstArrayView<UE::NNE::FTensorDesc> InputDescs,
		TConstArrayView<UE::NNE::FTensorDesc> OutputDescs,
		UE::NNE::FTensorShape& OutInputShape) const;
	bool FinalizeBuffers(
		FYUFSSharedOnnxModelState& State,
		TConstArrayView<UE::NNE::FTensorShape> OutputShapes,
		const UE::NNE::FTensorDesc& OutputDesc,
		const UE::NNE::FTensorShape& InputShape);
	bool PrepareInferenceBindings(FYUFSSharedOnnxModelState& State);

	FYUFSRuleBasedPolicy FallbackPolicy;
	FString ModelPath;
	FString RuntimeName;
	FString ResolvedModelPath;
	bool bLoadAttempted    = false;
	bool bModelReady       = false;
	bool bDataCollectionMode = false;
	TSharedPtr<FYUFSSharedOnnxModelState> SharedModelState;
};
