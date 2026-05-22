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

class YUFS_API FYUFSRLPolicy : public IYUFSDecisionPolicy
{
public:
	FYUFSRLPolicy(
		const FString& InModelPath = FString(),
		const FString& InRuntimeName = TEXT("NNERuntimeORTCpu"));

	virtual EYUFSAction SelectAction(const FYUFSNPCObservation& Obs) override;
	virtual void OnTransition(
		const FYUFSNPCObservation& PrevObs,
		EYUFSAction Action,
		const FYUFSNPCObservation& NextObs,
		float Reward,
		bool bDone) override;
	virtual bool IsLearningMode() const override { return false; }
	virtual void LoadModel(const FString& Path) override;

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

	FYUFSRuleBasedPolicy FallbackPolicy;
	FString ModelPath;
	FString RuntimeName;
	FString ResolvedModelPath;
	bool bLoadAttempted = false;
	bool bModelReady = false;
	TStrongObjectPtr<UNNEModelData> ModelData;
	TSharedPtr<UE::NNE::IModelCPU> CpuModel;
	TSharedPtr<UE::NNE::IModelInstanceCPU> CpuModelInstance;
	TSharedPtr<UE::NNE::IModelGPU> GpuModel;
	TSharedPtr<UE::NNE::IModelInstanceGPU> GpuModelInstance;
	TArray<float> InputBuffer;
	TArray<float> OutputBuffer;
};
