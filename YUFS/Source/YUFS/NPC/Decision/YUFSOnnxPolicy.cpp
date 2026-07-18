// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/Decision/YUFSOnnxPolicy.h"

#include "NNE.h"
#include "NNEModelData.h"
#include "HAL/FileManager.h"
#include "HAL/CriticalSection.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "UObject/StrongObjectPtr.h"

struct FYUFSSharedOnnxModelState
{
	TStrongObjectPtr<UNNEModelData> ModelData;
	TSharedPtr<UE::NNE::IModelCPU> CpuModel;
	TSharedPtr<UE::NNE::IModelInstanceCPU> CpuModelInstance;
	TSharedPtr<UE::NNE::IModelGPU> GpuModel;
	TSharedPtr<UE::NNE::IModelInstanceGPU> GpuModelInstance;
	TArray<float> InputBuffer;
	TArray<float> OutputBuffer;
	TArray<UE::NNE::FTensorBindingCPU> InputBindings;
	TArray<UE::NNE::FTensorBindingCPU> OutputBindings;
};

namespace
{
	const TCHAR* DefaultOrtCpuRuntimeName = TEXT("NNERuntimeORTCpu");
	const TCHAR* DefaultOrtDmlRuntimeName = TEXT("NNERuntimeORTDml");
	TMap<FString, TWeakPtr<FYUFSSharedOnnxModelState>> SharedModelCache;
	FCriticalSection AutoSelectedModelPathMutex;
	bool bAutoModelPathSelectionAttempted = false;
	FString AutoSelectedModelPath;

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

	Obs.FillFloatArray(SharedModelState->InputBuffer);
	const bool bInferenceSucceeded = SharedModelState->CpuModelInstance.IsValid()
		? RunCpuInference()
		: RunGpuInference();

	if (!bInferenceSucceeded)
	{
		return FallbackPolicy.SelectAction(Obs);
	}

	return SelectActionFromLogits(SharedModelState->OutputBuffer);
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

	const FString CacheKey = RuntimeName.ToLower() + TEXT("|") + ResolvedModelPath.ToLower();
	if (const TWeakPtr<FYUFSSharedOnnxModelState>* CachedModel = SharedModelCache.Find(CacheKey))
	{
		SharedModelState = CachedModel->Pin();
		if (SharedModelState.IsValid())
		{
			bModelReady = true;
			return true;
		}

		SharedModelCache.Remove(CacheKey);
	}

	TArray64<uint8> ModelBytes;
	if (!FFileHelper::LoadFileToArray(ModelBytes, *ResolvedModelPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: Failed to load ONNX model file: %s"), *ResolvedModelPath);
		return false;
	}

	TSharedPtr<FYUFSSharedOnnxModelState> NewSharedState = MakeShared<FYUFSSharedOnnxModelState>();

	if (RuntimeName.Equals(DefaultOrtDmlRuntimeName, ESearchCase::IgnoreCase))
	{
		bModelReady = LoadGpuModel(*NewSharedState, ResolvedModelPath, ModelBytes);
	}
	else
	{
		bModelReady = LoadCpuModel(*NewSharedState, ResolvedModelPath, ModelBytes);
	}

	if (bModelReady)
	{
		SharedModelState = MoveTemp(NewSharedState);
		SharedModelCache.Add(CacheKey, SharedModelState);
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

bool FYUFSOnnxPolicy::LoadCpuModel(FYUFSSharedOnnxModelState& State, const FString& ResolvedPath, const TArray64<uint8>& ModelBytes)
{
	TWeakInterfacePtr<INNERuntimeCPU> Runtime = UE::NNE::GetRuntime<INNERuntimeCPU>(RuntimeName);
	if (!Runtime.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: CPU runtime '%s' is not available."), *RuntimeName);
		return false;
	}

	State.ModelData.Reset(NewObject<UNNEModelData>(GetTransientPackage()));
	State.ModelData->Init(TEXT("onnx"), ModelBytes);

	if (Runtime->CanCreateModelCPU(State.ModelData.Get()) != INNERuntimeCPU::ECanCreateModelCPUStatus::Ok)
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: Runtime '%s' can not create CPU model from '%s'."), *RuntimeName, *ResolvedPath);
		return false;
	}

	State.CpuModel = Runtime->CreateModelCPU(State.ModelData.Get());
	if (!State.CpuModel.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: Failed to create CPU model for '%s'."), *ResolvedPath);
		return false;
	}

	State.CpuModelInstance = State.CpuModel->CreateModelInstanceCPU();
	if (!State.CpuModelInstance.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: Failed to create CPU model instance for '%s'."), *ResolvedPath);
		return false;
	}

	return ConfigureCpuModelInstance(State);
}

bool FYUFSOnnxPolicy::LoadGpuModel(FYUFSSharedOnnxModelState& State, const FString& ResolvedPath, const TArray64<uint8>& ModelBytes)
{
	TWeakInterfacePtr<INNERuntimeGPU> Runtime = UE::NNE::GetRuntime<INNERuntimeGPU>(RuntimeName);
	if (!Runtime.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: GPU runtime '%s' is not available."), *RuntimeName);
		return false;
	}

	State.ModelData.Reset(NewObject<UNNEModelData>(GetTransientPackage()));
	State.ModelData->Init(TEXT("onnx"), ModelBytes);

	if (Runtime->CanCreateModelGPU(State.ModelData.Get()) != INNERuntimeGPU::ECanCreateModelGPUStatus::Ok)
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: Runtime '%s' can not create GPU model from '%s'."), *RuntimeName, *ResolvedPath);
		return false;
	}

	State.GpuModel = Runtime->CreateModelGPU(State.ModelData.Get());
	if (!State.GpuModel.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: Failed to create GPU model for '%s'."), *ResolvedPath);
		return false;
	}

	State.GpuModelInstance = State.GpuModel->CreateModelInstanceGPU();
	if (!State.GpuModelInstance.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: Failed to create GPU model instance for '%s'."), *ResolvedPath);
		return false;
	}

	return ConfigureGpuModelInstance(State);
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
	FYUFSSharedOnnxModelState& State,
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

	const int32 InputElementCount = static_cast<int32>(InputShape.Volume());
	if (InputElementCount != FYUFSNPCObservation::FeatureCount)
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: Model expects %d input values, but observations provide %d."),
			InputElementCount, FYUFSNPCObservation::FeatureCount);
		return false;
	}

	State.InputBuffer.SetNumZeroed(InputElementCount);
	State.OutputBuffer.SetNumZeroed(static_cast<int32>(OutputShape.Volume()));
	return PrepareInferenceBindings(State);
}

bool FYUFSOnnxPolicy::PrepareInferenceBindings(FYUFSSharedOnnxModelState& State)
{
	if (State.InputBuffer.Num() != FYUFSNPCObservation::FeatureCount || State.OutputBuffer.IsEmpty())
	{
		return false;
	}

	State.InputBindings = { { State.InputBuffer.GetData(), static_cast<uint64>(State.InputBuffer.Num() * sizeof(float)) } };
	State.OutputBindings = { { State.OutputBuffer.GetData(), static_cast<uint64>(State.OutputBuffer.Num() * sizeof(float)) } };
	return true;
}

bool FYUFSOnnxPolicy::ConfigureCpuModelInstance(FYUFSSharedOnnxModelState& State)
{
	UE::NNE::FTensorShape InputShape;
	const TConstArrayView<UE::NNE::FTensorDesc> OutputDescs = State.CpuModelInstance->GetOutputTensorDescs();
	if (!ParseInputShape(State.CpuModelInstance->GetInputTensorDescs(), OutputDescs, InputShape))
		return false;

	if (State.CpuModelInstance->SetInputTensorShapes({ InputShape }) != UE::NNE::IModelInstanceCPU::ESetInputTensorShapesStatus::Ok)
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: Failed to set CPU input tensor shape for '%s'."), *ResolvedModelPath);
		return false;
	}

	return FinalizeBuffers(State, State.CpuModelInstance->GetOutputTensorShapes(), OutputDescs[0], InputShape);
}

bool FYUFSOnnxPolicy::ConfigureGpuModelInstance(FYUFSSharedOnnxModelState& State)
{
	UE::NNE::FTensorShape InputShape;
	const TConstArrayView<UE::NNE::FTensorDesc> OutputDescs = State.GpuModelInstance->GetOutputTensorDescs();
	if (!ParseInputShape(State.GpuModelInstance->GetInputTensorDescs(), OutputDescs, InputShape))
		return false;

	if (State.GpuModelInstance->SetInputTensorShapes({ InputShape }) != UE::NNE::IModelInstanceGPU::ESetInputTensorShapesStatus::Ok)
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: Failed to set GPU input tensor shape for '%s'."), *ResolvedModelPath);
		return false;
	}

	return FinalizeBuffers(State, State.GpuModelInstance->GetOutputTensorShapes(), OutputDescs[0], InputShape);
}

bool FYUFSOnnxPolicy::RunCpuInference()
{
	if (!SharedModelState.IsValid() || !SharedModelState->CpuModelInstance.IsValid()) return false;

	if (SharedModelState->CpuModelInstance->RunSync(SharedModelState->InputBindings, SharedModelState->OutputBindings)
		!= UE::NNE::IModelInstanceCPU::ERunSyncStatus::Ok)
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: CPU RunSync failed for model '%s'."), *ResolvedModelPath);
		return false;
	}

	return true;
}

bool FYUFSOnnxPolicy::RunGpuInference()
{
	if (!SharedModelState.IsValid() || !SharedModelState->GpuModelInstance.IsValid()) return false;

	if (SharedModelState->GpuModelInstance->RunSync(SharedModelState->InputBindings, SharedModelState->OutputBindings)
		!= UE::NNE::IModelInstanceGPU::ERunSyncStatus::Ok)
	{
		UE_LOG(LogTemp, Warning, TEXT("FYUFSOnnxPolicy: GPU RunSync failed for model '%s'."), *ResolvedModelPath);
		return false;
	}

	return true;
}

FString FYUFSOnnxPolicy::FindLatestOnnxModelPath()
{
	// 모델 경로가 명시되지 않은 경우 최초 탐색 결과를 프로세스 수명 동안 고정한다.
	// NPC마다 Saved 디렉터리를 다시 탐색하면 파일 타임스탬프가 비슷한 모델 중
	// 서로 다른 경로를 선택할 수 있으므로, 빈 결과도 포함해 한 번만 탐색한다.
	FScopeLock Lock(&AutoSelectedModelPathMutex);
	if (bAutoModelPathSelectionAttempted)
	{
		return AutoSelectedModelPath;
	}

	bAutoModelPathSelectionAttempted = true;

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
		return AutoSelectedModelPath;
	}

	FoundFiles.Sort([](const FString& Left, const FString& Right)
	{
		const FDateTime LeftTimestamp = IFileManager::Get().GetTimeStamp(*Left);
		const FDateTime RightTimestamp = IFileManager::Get().GetTimeStamp(*Right);
		if (LeftTimestamp != RightTimestamp)
		{
			return LeftTimestamp > RightTimestamp;
		}

		// 동일 타임스탬프에서도 실행마다 결과가 달라지지 않도록 경로로 순서를 고정한다.
		return Left.Compare(Right, ESearchCase::IgnoreCase) < 0;
	});

	AutoSelectedModelPath = FoundFiles[0];
	return AutoSelectedModelPath;
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
	SharedModelState.Reset();
}
