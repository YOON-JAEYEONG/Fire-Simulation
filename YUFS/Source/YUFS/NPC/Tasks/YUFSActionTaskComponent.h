#pragma once

#include "Components/ActorComponent.h"
#include "Core/YUFSTypes.h"
#include "CoreMinimal.h"
#include "YUFSActionTaskComponent.generated.h"

class FYUFSDeterministicRngSet;

USTRUCT(BlueprintType)
struct FYUFSLogNormalDurationModel
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Duration", meta=(ClampMin="0.1"))
	float MedianSeconds = 10.f;

	UPROPERTY(EditAnywhere, Category="Duration", meta=(ClampMin="0.0"))
	float Sigma = 0.45f;

	UPROPERTY(EditAnywhere, Category="Duration", meta=(ClampMin="0.1"))
	float CapSeconds = 60.f;
};

/** 기존 Action 위에 중단 가능한 작업 수명주기를 제공한다. */
UCLASS(ClassGroup=(YUFS), meta=(BlueprintSpawnableComponent))
class YUFS_API UYUFSActionTaskComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UYUFSActionTaskComponent();

	void UpdateTask(
		float DeltaTime,
		EYUFSAction Action,
		EYUFSIntent Intent,
		bool bImmediateLifeRisk,
		bool bOfficialInstruction,
		FYUFSDeterministicRngSet& RandomSource);

	EYUFSActionTask GetCurrentTask() const { return CurrentTask; }
	float GetProgress01() const;
	bool ConsumeTaskEvent(EYUFSActionTask& OutFrom, EYUFSActionTask& OutTo, EYUFSTaskCancelReason& OutReason);

	UPROPERTY(EditAnywhere, Category="Task|Duration")
	FYUFSLogNormalDurationModel SeekInformationDuration { 14.f, 0.45f, 45.f };
	UPROPERTY(EditAnywhere, Category="Task|Duration")
	FYUFSLogNormalDurationModel WaitObserveDuration { 35.f, 0.55f, 120.f };
	UPROPERTY(EditAnywhere, Category="Task|Duration")
	FYUFSLogNormalDurationModel GatherBelongingsDuration { 20.f, 0.45f, 60.f };
	UPROPERTY(EditAnywhere, Category="Task|Duration")
	FYUFSLogNormalDurationModel WarnHelpDuration { 28.f, 0.55f, 120.f };
	UPROPERTY(EditAnywhere, Category="Task|Duration")
	FVector2D FilmDurationRange = FVector2D(5.f, 15.f);
	UPROPERTY(EditAnywhere, Category="Task|Duration")
	FVector SuppressTriangularSeconds = FVector(15.f, 20.f, 30.f);

private:
	struct FTaskEvent
	{
		EYUFSActionTask From = EYUFSActionTask::None;
		EYUFSActionTask To = EYUFSActionTask::None;
		EYUFSTaskCancelReason Reason = EYUFSTaskCancelReason::None;
	};

	static EYUFSActionTask MapActionToTask(EYUFSAction Action);
	static float DrawLogNormal(const FYUFSLogNormalDurationModel& Model, FYUFSDeterministicRngSet& RandomSource);
	static float DrawTriangular(const FVector& MinModeMax, FYUFSDeterministicRngSet& RandomSource);
	float DrawDuration(EYUFSActionTask Task, FYUFSDeterministicRngSet& RandomSource) const;
	void StartTask(EYUFSActionTask NewTask, FYUFSDeterministicRngSet& RandomSource);
	void CancelTask(EYUFSTaskCancelReason Reason);

	EYUFSActionTask CurrentTask = EYUFSActionTask::None;
	EYUFSActionTask CompletedTask = EYUFSActionTask::None;
	EYUFSAction LastObservedAction = EYUFSAction::Idle;
	EYUFSIntent LastObservedIntent = EYUFSIntent::Observe;
	TArray<FTaskEvent> PendingEvents;
	float ElapsedSeconds = 0.f;
	float PlannedDurationSeconds = 0.f;
	bool bHasObservedContext = false;
};
