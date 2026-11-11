// Fill out your copyright notice in the Description page of Project Settings.

#include "Simulation/YUFSScenarioConfig.h"

#define LOCTEXT_NAMESPACE "YUFSScenarioConfig"

bool FYUFSScenarioConfig::IsValid(FText& OutError) const
{
	if (SimulationMap.IsNull())
	{
		OutError = LOCTEXT("NoMap", "시뮬레이션 맵이 선택되지 않았습니다.");
		return false;
	}

	if (NPCCount < 0)
	{
		OutError = LOCTEXT("BadNPCCount", "NPC 수는 0 이상이어야 합니다.");
		return false;
	}

	if (TimelineRecordIntervalSeconds < 0.05f)
	{
		OutError = LOCTEXT("BadInterval", "타임라인 기록 간격은 0.05초 이상이어야 합니다.");
		return false;
	}

	if (PolicyType == EYUFSScenarioPolicy::ML && OnnxModelPath.IsEmpty())
	{
		OutError = LOCTEXT("NoModelPath", "ML 정책을 선택했지만 ONNX 모델 경로가 비어 있습니다.");
		return false;
	}

	OutError = FText::GetEmpty();
	return true;
}

#undef LOCTEXT_NAMESPACE
