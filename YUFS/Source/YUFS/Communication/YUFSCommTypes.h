// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

UENUM(BlueprintType)
enum class EYUFSCommType : uint8
{
	AlarmOnly, // 논문: delayed response 증가, filming 3.43배 증가
	PreRecordedMessage, // 논문: 알람보다 개선, filming 여전히 증가
	LiveAnnouncement, // 논문: delayed response 감소
	StaffGuidance // 논문: 가장 효과적 (OR=0.33), filming 미영향
};
