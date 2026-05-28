#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Simulation/YUFSTimelineTypes.h"
#include "YUFSTimelineRecorder.generated.h"

class AYUFSSimulationController;
class AYUFSHeterogeneousVolume;
class AYUFSEvacuationNPC;

/**
 * 화재 시작 후 지정 시간까지의 시뮬레이션 결과를 최소 정보로 기록하고,
 * 기록 종료 후 관찰 모드에서 시간 이동/재생/일시정지를 담당합니다.
 */
UCLASS(ClassGroup=(YUFS), meta=(BlueprintSpawnableComponent))
class YUFS_API UYUFSTimelineRecorder : public UActorComponent
{
	GENERATED_BODY()

public:
	UYUFSTimelineRecorder();

	void Initialize(AYUFSSimulationController* InController, AYUFSHeterogeneousVolume* InVolume);

	// 기록 시작/종료 ---------------------------------------------------------
	void BeginRecording(float InTargetFireTimeSeconds, float InRecordIntervalSeconds);
	void StopRecording();
	bool IsRecording() const { return bRecording; }

	// FireActive 단계에서 SimulationController가 호출합니다.
	void TickRecording(
		float DeltaTime,
		float FireElapsedTime,
		int32 FireFrame,
		const TArray<AYUFSEvacuationNPC*>& NPCs,
		int32 EvacuatedCount,
		int32 IncapacitatedCount);

	// 관찰 모드 -------------------------------------------------------------
	void EnterReviewMode(const TArray<AYUFSEvacuationNPC*>& NPCs);
	void PlayReview();
	void PauseReview();
	void TickReview(float DeltaTime, const TArray<AYUFSEvacuationNPC*>& NPCs);
	void SeekToFireTime(float TargetFireTime, const TArray<AYUFSEvacuationNPC*>& NPCs);

	UFUNCTION(BlueprintPure, Category="YUFS|Timeline")
	bool IsReviewPlaying() const { return bReviewPlaying; }

	UFUNCTION(BlueprintPure, Category="YUFS|Timeline")
	float GetCurrentReviewFireTime() const { return CurrentReviewFireTime; }

	UFUNCTION(BlueprintPure, Category="YUFS|Timeline")
	float GetMaxRecordedFireTime() const;

	UFUNCTION(BlueprintPure, Category="YUFS|Timeline")
	float GetTimelineProgress01() const;

	UFUNCTION(BlueprintPure, Category="YUFS|Timeline")
	int32 GetRecordedFrameCount() const { return RecordedFrames.Num(); }

	const TArray<FYUFSTimelineFrame>& GetRecordedFrames() const { return RecordedFrames; }

private:
	UPROPERTY()
	AYUFSSimulationController* OwnerController = nullptr;

	UPROPERTY()
	AYUFSHeterogeneousVolume* HeterogeneousVolume = nullptr;

	UPROPERTY()
	TArray<FYUFSTimelineFrame> RecordedFrames;

	bool bRecording = false;
	bool bReviewPlaying = false;

	float TargetFireTimeSeconds = 0.f;
	float RecordIntervalSeconds = 0.25f;
	float TimeSinceLastRecord = 0.f;
	float CurrentReviewFireTime = 0.f;

	void CaptureFrame(
		float FireElapsedTime,
		int32 FireFrame,
		const TArray<AYUFSEvacuationNPC*>& NPCs,
		int32 EvacuatedCount,
		int32 IncapacitatedCount);

	const FYUFSTimelineFrame* FindNearestFrame(float TargetFireTime) const;
	void ApplyFrame(const FYUFSTimelineFrame& Frame, const TArray<AYUFSEvacuationNPC*>& NPCs);
};
