// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Core/YUFSTypes.h"
#include "YUFSDecisionPolicy.generated.h"

struct FYUFSNPCObservation;
// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class UYUFSDecisionPolicy : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class YUFS_API IYUFSDecisionPolicy
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
	virtual EYUFSAction SelectAction(const FYUFSNPCObservation& Obs) = 0;

	// 데이터 수집 모드: ONNX 모델 대신 룰베이스 폴백을 강제해 학습 데이터를 수집
	virtual bool IsDataCollectionMode() const { return false; }

	virtual void LoadModel(const FString& Path) {}
};
