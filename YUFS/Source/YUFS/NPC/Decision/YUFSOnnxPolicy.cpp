// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/Decision/YUFSOnnxPolicy.h"

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

FYUFSOnnxPolicy::FYUFSOnnxPolicy(const FString& InModelPath, const FString& InRuntimeName)
	: ModelPath(InModelPath)
	, RuntimeName(InRuntimeName.IsEmpty() ? DefaultOrtCpuRuntimeName : InRuntimeName)
{
}

EYUFSAction FYUFSOnnxPolicy::SelectAction(const FYUFSNPCObservation& Obs)
{
	if (bDataCollectionMode || !EnsureModelLoaded())
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

void FYUFSOnnxPolicy::LoadModel(const FString& Path)
{
	ModelPath = Path;
	ResetLoadedModelState();
}

bool FYUFSOnnxPolicy::EnsureModelLoaded()
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
			UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: OnnxModelPath is empty and no .onnx model was found under the project Saved directory. Falling back to rule-based policy."));
			return false;
		}

		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: OnnxModelPath was empty. Auto-selected latest ONNX model: %s"), *ModelPath);
	}

	ResolvedModelPath = ResolveModelPath(ModelPath);
	if (!FPaths::FileExists(ResolvedModelPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: ONNX model file not found: %s"), *ResolvedModelPath);
		return false;
	}

	TArray64<uint8> ModelBytes;
	if (!FFileHelper::LoadFileToArray(ModelBytes, *ResolvedModelPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: Failed to load ONNX model file: %s"), *ResolvedModelPath);
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
			TEXT("FYUFSOnnxPolicy: Failed to initialize runtime '%s' for model '%s'. Falling back to rule-based policy."),
			*RuntimeName,
			*ResolvedModelPath);
	}

	return bModelReady;
}

bool FYUFSOnnxPolicy::LoadCpuModel(const FString& ResolvedPath, const TArray64<uint8>& ModelBytes)
{
	TWeakInterfacePtr<INNERuntimeCPU> Runtime = UE::NNE::GetRuntime<INNERuntimeCPU>(RuntimeName);
	if (!Runtime.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: CPU runtime '%s' is not available."), *RuntimeName);
		return false;
	}

	ModelData.Reset(NewObject<UNNEModelData>(GetTransientPackage()));
	ModelData->Init(TEXT("onnx"), ModelBytes);

	if (Runtime->CanCreateModelCPU(ModelData.Get()) != INNERuntimeCPU::ECanCreateModelCPUStatus::Ok)
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: Runtime '%s' can not create CPU model from '%s'."), *RuntimeName, *ResolvedPath);
		return false;
	}

	CpuModel = Runtime->CreateModelCPU(ModelData.Get());
	if (!CpuModel.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: Failed to create CPU model for '%s'."), *ResolvedPath);
		return false;
	}

	CpuModelInstance = CpuModel->CreateModelInstanceCPU();
	if (!CpuModelInstance.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: Failed to create CPU model instance for '%s'."), *ResolvedPath);
		return false;
	}

	return ConfigureCpuModelInstance();
}

bool FYUFSOnnxPolicy::LoadGpuModel(const FString& ResolvedPath, const TArray64<uint8>& ModelBytes)
{
	TWeakInterfacePtr<INNERuntimeGPU> Runtime = UE::NNE::GetRuntime<INNERuntimeGPU>(RuntimeName);
	if (!Runtime.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: GPU runtime '%s' is not available."), *RuntimeName);
		return false;
	}

	ModelData.Reset(NewObject<UNNEModelData>(GetTransientPackage()));
	ModelData->Init(TEXT("onnx"), ModelBytes);

	if (Runtime->CanCreateModelGPU(ModelData.Get()) != INNERuntimeGPU::ECanCreateModelGPUStatus::Ok)
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: Runtime '%s' can not create GPU model from '%s'."), *RuntimeName, *ResolvedPath);
		return false;
	}

	GpuModel = Runtime->CreateModelGPU(ModelData.Get());
	if (!GpuModel.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: Failed to create GPU model for '%s'."), *ResolvedPath);
		return false;
	}

	GpuModelInstance = GpuModel->CreateModelInstanceGPU();
	if (!GpuModelInstance.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: Failed to create GPU model instance for '%s'."), *ResolvedPath);
		return false;
	}

	return ConfigureGpuModelInstance();
}

bool FYUFSOnnxPolicy::ParseInputShape(
	TConstArrayView<UE::NNE::FTensorDesc> InputDescs,
	TConstArrayView<UE::NNE::FTensorDesc> OutputDescs,
	UE::NNE::FTensorShape& OutInputShape) const
{
	if (InputDescs.Num() != 1 || OutputDescs.Num() != 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: Expected exactly one input and one output tensor."));
		return false;
	}

	const UE::NNE::FSymbolicTensorShape& SymbolicShape = InputDescs[0].GetShape();
	if (SymbolicShape.Rank() != 2)
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: Expected rank-2 input tensor, got rank %d."), SymbolicShape.Rank());
		return false;
	}

	const TConstArrayView<int32> Dims = SymbolicShape.GetData();
	const uint32 Width = static_cast<uint32>(Dims[1] > 0 ? Dims[1] : 28);
	OutInputShape = UE::NNE::FTensorShape::Make({ 1u, Width });
	return true;
}

bool FYUFSOnnxPolicy::FinalizeBuffers(
	TConstArrayView<UE::NNE::FTensorShape> OutputShapes,
	const UE::NNE::FTensorDesc& OutputDesc,
	const UE::NNE::FTensorShape& InputShape)
{
	const UE::NNE::FTensorShape OutputShape = ResolveOutputShape(OutputShapes, OutputDesc);
	if (OutputShape.Volume() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: Failed to resolve output tensor shape for '%s'."), *ResolvedModelPath);
		return false;
	}

	InputBuffer.SetNumZeroed(static_cast<int32>(InputShape.Volume()));
	OutputBuffer.SetNumZeroed(static_cast<int32>(OutputShape.Volume()));
	return OutputBuffer.Num() > 0;
}

bool FYUFSOnnxPolicy::PrepareInferenceBindings(
	const TArray<float>& StateVec,
	TArray<UE::NNE::FTensorBindingCPU>& OutInputBindings,
	TArray<UE::NNE::FTensorBindingCPU>& OutOutputBindings)
{
	if (StateVec.Num() != InputBuffer.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: State dimension mismatch. Expected %d but got %d."),
			InputBuffer.Num(), StateVec.Num());
		return false;
	}

	InputBuffer = StateVec;
	OutInputBindings  = { { InputBuffer.GetData(),  static_cast<uint64>(InputBuffer.Num()  * sizeof(float)) } };
	OutOutputBindings = { { OutputBuffer.GetData(), static_cast<uint64>(OutputBuffer.Num() * sizeof(float)) } };
	return true;
}

bool FYUFSOnnxPolicy::ConfigureCpuModelInstance()
{
	UE::NNE::FTensorShape InputShape;
	const TConstArrayView<UE::NNE::FTensorDesc> OutputDescs = CpuModelInstance->GetOutputTensorDescs();
	if (!ParseInputShape(CpuModelInstance->GetInputTensorDescs(), OutputDescs, InputShape))
		return false;

	if (CpuModelInstance->SetInputTensorShapes({ InputShape }) != UE::NNE::IModelInstanceCPU::ESetInputTensorShapesStatus::Ok)
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: Failed to set CPU input tensor shape for '%s'."), *ResolvedModelPath);
		return false;
	}

	return FinalizeBuffers(CpuModelInstance->GetOutputTensorShapes(), OutputDescs[0], InputShape);
}

bool FYUFSOnnxPolicy::ConfigureGpuModelInstance()
{
	UE::NNE::FTensorShape InputShape;
	const TConstArrayView<UE::NNE::FTensorDesc> OutputDescs = GpuModelInstance->GetOutputTensorDescs();
	if (!ParseInputShape(GpuModelInstance->GetInputTensorDescs(), OutputDescs, InputShape))
		return false;

	if (GpuModelInstance->SetInputTensorShapes({ InputShape }) != UE::NNE::IModelInstanceGPU::ESetInputTensorShapesStatus::Ok)
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: Failed to set GPU input tensor shape for '%s'."), *ResolvedModelPath);
		return false;
	}

	return FinalizeBuffers(GpuModelInstance->GetOutputTensorShapes(), OutputDescs[0], InputShape);
}

bool FYUFSOnnxPolicy::RunCpuInference(const TArray<float>& StateVec, TArray<float>& OutLogits)
{
	if (!CpuModelInstance.IsValid()) return false;

	TArray<UE::NNE::FTensorBindingCPU> InputBindings, OutputBindings;
	if (!PrepareInferenceBindings(StateVec, InputBindings, OutputBindings)) return false;

	if (CpuModelInstance->RunSync(InputBindings, OutputBindings) != UE::NNE::IModelInstanceCPU::ERunSyncStatus::Ok)
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: CPU RunSync failed for model '%s'."), *ResolvedModelPath);
		return false;
	}

	OutLogits = OutputBuffer;
	return true;
}

bool FYUFSOnnxPolicy::RunGpuInference(const TArray<float>& StateVec, TArray<float>& OutLogits)
{
	if (!GpuModelInstance.IsValid()) return false;

	TArray<UE::NNE::FTensorBindingCPU> InputBindings, OutputBindings;
	if (!PrepareInferenceBindings(StateVec, InputBindings, OutputBindings)) return false;

	if (GpuModelInstance->RunSync(InputBindings, OutputBindings) != UE::NNE::IModelInstanceGPU::ERunSyncStatus::Ok)
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: GPU RunSync failed for model '%s'."), *ResolvedModelPath);
		return false;
	}

	OutLogits = OutputBuffer;
	return true;
}

FString FYUFSOnnxPolicy::FindLatestOnnxModelPath()
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

FString FYUFSOnnxPolicy::ResolveModelPath(const FString& InPath)
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

EYUFSAction FYUFSOnnxPolicy::SelectActionFromLogits(const TArray<float>& Logits) const
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

void FYUFSOnnxPolicy::ResetLoadedModelState()
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
