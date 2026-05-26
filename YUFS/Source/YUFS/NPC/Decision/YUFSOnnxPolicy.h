// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Core/YUFSDecisionPolicy.h"
#include "Core/YUFSObservation.h"
#include "NNEModelData.h"
#include "NNERuntimeCPU.h"
#include "NNERuntimeGPU.h"
#include "UObject/StrongObjectPtr.h"
#include "YUFSRuleBasedPolicy.h"

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
	bool LoadCpuModel(const FString& ResolvedPath, const TArray64<uint8>& ModelBytes);
	bool LoadGpuModel(const FString& ResolvedPath, const TArray64<uint8>& ModelBytes);
	bool ConfigureCpuModelInstance();
	bool ConfigureGpuModelInstance();
	bool RunCpuInference(const TArray<float>& StateVec, TArray<float>& OutLogits);
	bool RunGpuInference(const TArray<float>& StateVec, TArray<float>& OutLogits);
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
		TConstArrayView<UE::NNE::FTensorShape> OutputShapes,
		const UE::NNE::FTensorDesc& OutputDesc,
		const UE::NNE::FTensorShape& InputShape);
	bool PrepareInferenceBindings(
		const TArray<float>& StateVec,
		TArray<UE::NNE::FTensorBindingCPU>& OutInputBindings,
		TArray<UE::NNE::FTensorBindingCPU>& OutOutputBindings);

	FYUFSRuleBasedPolicy FallbackPolicy;
	FString ModelPath;
	FString RuntimeName;
	FString ResolvedModelPath;
	bool bLoadAttempted    = false;
	bool bModelReady       = false;
	bool bDataCollectionMode = false;
	TStrongObjectPtr<UNNEModelData> ModelData;
	TSharedPtr<UE::NNE::IModelCPU> CpuModel;
	TSharedPtr<UE::NNE::IModelInstanceCPU> CpuModelInstance;
	TSharedPtr<UE::NNE::IModelGPU> GpuModel;
	TSharedPtr<UE::NNE::IModelInstanceGPU> GpuModelInstance;
	TArray<float> InputBuffer;
	TArray<float> OutputBuffer;
};
