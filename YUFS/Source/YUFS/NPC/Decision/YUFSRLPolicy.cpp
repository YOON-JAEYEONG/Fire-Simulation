// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/Decision/YUFSRLPolicy.h"

#include "NNE.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	const TCHAR* DefaultOrtCpuRuntimeName = TEXT("NNERuntimeORTCpu");
	const TCHAR* DefaultOrtDmlRuntimeName = TEXT("NNERuntimeORTDml");

	UE::NNE::FTensorShape ResolveOutputShape(
		TConstArrayView<UE::NNE::FTensorShape> OutputTensorShapes,
		const UE::NNE::FTensorDesc& OutputTensorDesc)
	{
		if (OutputTensorShapes.Num() == 1)
		{
			return OutputTensorShapes[0];
		}

		return UE::NNE::FTensorShape::MakeFromSymbolic(OutputTensorDesc.GetShape());
	}
}

FYUFSRLPolicy::FYUFSRLPolicy(const FString& InModelPath, const FString& InRuntimeName)
	: ModelPath(InModelPath)
	, RuntimeName(InRuntimeName.IsEmpty() ? DefaultOrtCpuRuntimeName : InRuntimeName)
{
}

EYUFSAction FYUFSRLPolicy::SelectAction(const FYUFSNPCObservation& Obs)
{
	if (bForceFallback || !EnsureModelLoaded())
	{
		return FallbackPolicy.SelectAction(Obs);
	}

	const TArray<float> StateVec = Obs.ToFloatArray();
	TArray<float> Logits;
	const bool bInferenceSucceeded = CpuModelInstance.IsValid()
		? RunCpuInference(StateVec, Logits)
		: RunGpuInference(StateVec, Logits);

	if (!bInferenceSucceeded)
	{
		return FallbackPolicy.SelectAction(Obs);
	}

	return SelectActionFromLogits(Logits);
}

void FYUFSRLPolicy::OnTransition(
	const FYUFSNPCObservation& PrevObs,
	EYUFSAction Action,
	const FYUFSNPCObservation& NextObs,
	float Reward,
	bool bDone)
{
}

void FYUFSRLPolicy::LoadModel(const FString& Path)
{
	ModelPath = Path;
	ResetLoadedModelState();
}

bool FYUFSRLPolicy::EnsureModelLoaded()
{
	if (bModelReady)
	{
		return true;
	}

	if (bLoadAttempted)
	{
		return false;
	}

	bLoadAttempted = true;

	if (ModelPath.IsEmpty())
	{
		ModelPath = FindLatestOnnxModelPath();
		if (ModelPath.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("FYUFSRLPolicy: RLModelPath is empty and no .onnx model was found under the project Saved directory. Falling back to rule-based policy."));
			return false;
		}

		UE_LOG(LogTemp, Warning, TEXT("FYUFSRLPolicy: RLModelPath was empty. Auto-selected latest ONNX model: %s"), *ModelPath);
	}

	ResolvedModelPath = ResolveModelPath(ModelPath);
	if (!FPaths::FileExists(ResolvedModelPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSRLPolicy: ONNX model file not found: %s"), *ResolvedModelPath);
		return false;
	}

	TArray64<uint8> ModelBytes;
	if (!FFileHelper::LoadFileToArray(ModelBytes, *ResolvedModelPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSRLPolicy: Failed to load ONNX model file: %s"), *ResolvedModelPath);
		return false;
	}

	if (RuntimeName.Equals(DefaultOrtDmlRuntimeName, ESearchCase::IgnoreCase))
	{
		bModelReady = LoadGpuModel(ResolvedModelPath, ModelBytes);
	}
	else
	{
		bModelReady = LoadCpuModel(ResolvedModelPath, ModelBytes);
	}

	if (!bModelReady)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("FYUFSRLPolicy: Failed to initialize runtime '%s' for model '%s'. Falling back to rule-based policy."),
			*RuntimeName,
			*ResolvedModelPath);
	}

	return bModelReady;
}

bool FYUFSRLPolicy::LoadCpuModel(const FString& ResolvedPath, const TArray64<uint8>& ModelBytes)
{
	TWeakInterfacePtr<INNERuntimeCPU> Runtime = UE::NNE::GetRuntime<INNERuntimeCPU>(RuntimeName);
	if (!Runtime.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSRLPolicy: CPU runtime '%s' is not available."), *RuntimeName);
		return false;
	}

	ModelData.Reset(NewObject<UNNEModelData>(GetTransientPackage()));
	ModelData->Init(TEXT("onnx"), ModelBytes);

	if (Runtime->CanCreateModelCPU(ModelData.Get()) != INNERuntimeCPU::ECanCreateModelCPUStatus::Ok)
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSRLPolicy: Runtime '%s' can not create CPU model from '%s'."), *RuntimeName, *ResolvedPath);
		return false;
	}

	CpuModel = Runtime->CreateModelCPU(ModelData.Get());
	if (!CpuModel.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSRLPolicy: Failed to create CPU model for '%s'."), *ResolvedPath);
		return false;
	}

	CpuModelInstance = CpuModel->CreateModelInstanceCPU();
	if (!CpuModelInstance.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSRLPolicy: Failed to create CPU model instance for '%s'."), *ResolvedPath);
		return false;
	}

	return ConfigureCpuModelInstance();
}

bool FYUFSRLPolicy::LoadGpuModel(const FString& ResolvedPath, const TArray64<uint8>& ModelBytes)
{
	TWeakInterfacePtr<INNERuntimeGPU> Runtime = UE::NNE::GetRuntime<INNERuntimeGPU>(RuntimeName);
	if (!Runtime.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSRLPolicy: GPU runtime '%s' is not available."), *RuntimeName);
		return false;
	}

	ModelData.Reset(NewObject<UNNEModelData>(GetTransientPackage()));
	ModelData->Init(TEXT("onnx"), ModelBytes);

	if (Runtime->CanCreateModelGPU(ModelData.Get()) != INNERuntimeGPU::ECanCreateModelGPUStatus::Ok)
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSRLPolicy: Runtime '%s' can not create GPU model from '%s'."), *RuntimeName, *ResolvedPath);
		return false;
	}

	GpuModel = Runtime->CreateModelGPU(ModelData.Get());
	if (!GpuModel.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSRLPolicy: Failed to create GPU model for '%s'."), *ResolvedPath);
		return false;
	}

	GpuModelInstance = GpuModel->CreateModelInstanceGPU();
	if (!GpuModelInstance.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSRLPolicy: Failed to create GPU model instance for '%s'."), *ResolvedPath);
		return false;
	}

	return ConfigureGpuModelInstance();
}

bool FYUFSRLPolicy::ConfigureCpuModelInstance()
{
	const TConstArrayView<UE::NNE::FTensorDesc> InputTensorDescs = CpuModelInstance->GetInputTensorDescs();
	const TConstArrayView<UE::NNE::FTensorDesc> OutputTensorDescs = CpuModelInstance->GetOutputTensorDescs();
	if (InputTensorDescs.Num() != 1 || OutputTensorDescs.Num() != 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSRLPolicy: Expected exactly one input and one output tensor."));
		return false;
	}

	const UE::NNE::FSymbolicTensorShape& InputSymbolicShape = InputTensorDescs[0].GetShape();
	if (InputSymbolicShape.Rank() != 2)
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSRLPolicy: Expected rank-2 input tensor, got rank %d."), InputSymbolicShape.Rank());
		return false;
	}

	const TConstArrayView<int32> InputDims = InputSymbolicShape.GetData();
	const uint32 InputWidth = static_cast<uint32>(InputDims[1] > 0 ? InputDims[1] : 29);
	const TArray<uint32> ConcreteInputShape = { 1u, InputWidth };
	const UE::NNE::FTensorShape InputShape = UE::NNE::FTensorShape::Make(ConcreteInputShape);

	if (CpuModelInstance->SetInputTensorShapes({ InputShape }) != UE::NNE::IModelInstanceCPU::ESetInputTensorShapesStatus::Ok)
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSRLPolicy: Failed to set CPU input tensor shape for '%s'."), *ResolvedModelPath);
		return false;
	}

	const TConstArrayView<UE::NNE::FTensorShape> OutputTensorShapes = CpuModelInstance->GetOutputTensorShapes();
	const UE::NNE::FTensorShape OutputShape = ResolveOutputShape(OutputTensorShapes, OutputTensorDescs[0]);
	if (OutputShape.Volume() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSRLPolicy: Failed to resolve CPU output tensor shape for '%s'."), *ResolvedModelPath);
		return false;
	}

	InputBuffer.SetNumZeroed(static_cast<int32>(InputShape.Volume()));
	OutputBuffer.SetNumZeroed(static_cast<int32>(OutputShape.Volume()));
	return OutputBuffer.Num() > 0;
}

bool FYUFSRLPolicy::ConfigureGpuModelInstance()
{
	const TConstArrayView<UE::NNE::FTensorDesc> InputTensorDescs = GpuModelInstance->GetInputTensorDescs();
	const TConstArrayView<UE::NNE::FTensorDesc> OutputTensorDescs = GpuModelInstance->GetOutputTensorDescs();
	if (InputTensorDescs.Num() != 1 || OutputTensorDescs.Num() != 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSRLPolicy: Expected exactly one input and one output tensor."));
		return false;
	}

	const UE::NNE::FSymbolicTensorShape& InputSymbolicShape = InputTensorDescs[0].GetShape();
	if (InputSymbolicShape.Rank() != 2)
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSRLPolicy: Expected rank-2 input tensor, got rank %d."), InputSymbolicShape.Rank());
		return false;
	}

	const TConstArrayView<int32> InputDims = InputSymbolicShape.GetData();
	const uint32 InputWidth = static_cast<uint32>(InputDims[1] > 0 ? InputDims[1] : 29);
	const TArray<uint32> ConcreteInputShape = { 1u, InputWidth };
	const UE::NNE::FTensorShape InputShape = UE::NNE::FTensorShape::Make(ConcreteInputShape);

	if (GpuModelInstance->SetInputTensorShapes({ InputShape }) != UE::NNE::IModelInstanceGPU::ESetInputTensorShapesStatus::Ok)
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSRLPolicy: Failed to set GPU input tensor shape for '%s'."), *ResolvedModelPath);
		return false;
	}

	const TConstArrayView<UE::NNE::FTensorShape> OutputTensorShapes = GpuModelInstance->GetOutputTensorShapes();
	const UE::NNE::FTensorShape OutputShape = ResolveOutputShape(OutputTensorShapes, OutputTensorDescs[0]);
	if (OutputShape.Volume() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSRLPolicy: Failed to resolve GPU output tensor shape for '%s'."), *ResolvedModelPath);
		return false;
	}

	InputBuffer.SetNumZeroed(static_cast<int32>(InputShape.Volume()));
	OutputBuffer.SetNumZeroed(static_cast<int32>(OutputShape.Volume()));
	return OutputBuffer.Num() > 0;
}

bool FYUFSRLPolicy::RunCpuInference(const TArray<float>& StateVec, TArray<float>& OutLogits)
{
	if (!CpuModelInstance.IsValid())
	{
		return false;
	}

	if (StateVec.Num() != InputBuffer.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSRLPolicy: State dimension mismatch. Expected %d but got %d."), InputBuffer.Num(), StateVec.Num());
		return false;
	}

	InputBuffer = StateVec;

	const TArray<UE::NNE::FTensorBindingCPU> InputBindings = {
		UE::NNE::FTensorBindingCPU{ InputBuffer.GetData(), static_cast<uint64>(InputBuffer.Num() * sizeof(float)) }
	};
	const TArray<UE::NNE::FTensorBindingCPU> OutputBindings = {
		UE::NNE::FTensorBindingCPU{ OutputBuffer.GetData(), static_cast<uint64>(OutputBuffer.Num() * sizeof(float)) }
	};

	if (CpuModelInstance->RunSync(InputBindings, OutputBindings) != UE::NNE::IModelInstanceCPU::ERunSyncStatus::Ok)
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSRLPolicy: CPU RunSync failed for model '%s'."), *ResolvedModelPath);
		return false;
	}

	OutLogits = OutputBuffer;
	return true;
}

bool FYUFSRLPolicy::RunGpuInference(const TArray<float>& StateVec, TArray<float>& OutLogits)
{
	if (!GpuModelInstance.IsValid())
	{
		return false;
	}

	if (StateVec.Num() != InputBuffer.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSRLPolicy: State dimension mismatch. Expected %d but got %d."), InputBuffer.Num(), StateVec.Num());
		return false;
	}

	InputBuffer = StateVec;

	const TArray<UE::NNE::FTensorBindingCPU> InputBindings = {
		UE::NNE::FTensorBindingCPU{ InputBuffer.GetData(), static_cast<uint64>(InputBuffer.Num() * sizeof(float)) }
	};
	const TArray<UE::NNE::FTensorBindingCPU> OutputBindings = {
		UE::NNE::FTensorBindingCPU{ OutputBuffer.GetData(), static_cast<uint64>(OutputBuffer.Num() * sizeof(float)) }
	};

	if (GpuModelInstance->RunSync(InputBindings, OutputBindings) != UE::NNE::IModelInstanceGPU::ERunSyncStatus::Ok)
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSRLPolicy: GPU RunSync failed for model '%s'."), *ResolvedModelPath);
		return false;
	}

	OutLogits = OutputBuffer;
	return true;
}

FString FYUFSRLPolicy::FindLatestOnnxModelPath()
{
	TArray<FString> FoundFiles;
	IFileManager::Get().FindFilesRecursive(
		FoundFiles,
		*FPaths::ProjectSavedDir(),
		TEXT("*.onnx"),
		true,
		false,
		false);

	if (FoundFiles.IsEmpty())
	{
		return FString();
	}

	FoundFiles.Sort([](const FString& Left, const FString& Right)
	{
		return IFileManager::Get().GetTimeStamp(*Left) > IFileManager::Get().GetTimeStamp(*Right);
	});

	return FoundFiles[0];
}

FString FYUFSRLPolicy::ResolveModelPath(const FString& InPath)
{
	if (InPath.IsEmpty())
	{
		return FString();
	}

	if (FPaths::FileExists(InPath))
	{
		return FPaths::ConvertRelativePathToFull(InPath);
	}

	const TArray<FString> Candidates = {
		FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), InPath),
		FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir(), InPath),
		FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir(), InPath)
	};

	for (const FString& Candidate : Candidates)
	{
		if (FPaths::FileExists(Candidate))
		{
			return Candidate;
		}
	}

	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), InPath);
}

EYUFSAction FYUFSRLPolicy::SelectActionFromLogits(const TArray<float>& Logits) const
{
	if (Logits.IsEmpty())
	{
		return EYUFSAction::Idle;
	}

	int32 BestIndex = 0;
	float BestValue = Logits[0];
	for (int32 Index = 1; Index < Logits.Num(); ++Index)
	{
		if (Logits[Index] > BestValue)
		{
			BestValue = Logits[Index];
			BestIndex = Index;
		}
	}

	return StaticEnum<EYUFSAction>()->IsValidEnumValue(BestIndex)
		? static_cast<EYUFSAction>(BestIndex)
		: EYUFSAction::Idle;
}

void FYUFSRLPolicy::ResetLoadedModelState()
{
	bLoadAttempted = false;
	bModelReady = false;
	ResolvedModelPath.Reset();
	ModelData.Reset();
	CpuModel.Reset();
	CpuModelInstance.Reset();
	GpuModel.Reset();
	GpuModelInstance.Reset();
	InputBuffer.Reset();
	OutputBuffer.Reset();
}
