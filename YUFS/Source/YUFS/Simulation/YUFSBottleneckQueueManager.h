// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "YUFSBottleneckQueueManager.generated.h"

class AYUFSBottleneckPoint;

USTRUCT()
struct FYUFSBottleneckQueueState
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<AYUFSBottleneckPoint> Bottleneck = nullptr;

	UPROPERTY()
	TArray<TWeakObjectPtr<AActor>> Occupants;
};

UCLASS()
class YUFS_API AYUFSBottleneckQueueManager : public AActor
{
	GENERATED_BODY()

public:
	AYUFSBottleneckQueueManager();

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

	FVector ResolveMovementTarget(AActor* Requester, const TArray<FVector>& CurrentPath, const FVector& FinalDestination);
	void ReleaseRequester(AActor* Requester);

private:
	void CollectBottlenecks();
	void CompactQueues();
	void CompactQueue(FYUFSBottleneckQueueState& QueueState);
	AYUFSBottleneckPoint* FindAssignedBottleneck(AActor* Requester, int32& OutQueueIndex);
	AYUFSBottleneckPoint* FindNextBottleneck(AActor* Requester, const TArray<FVector>& CurrentPath, const FVector& FinalDestination) const;
	FYUFSBottleneckQueueState& GetOrCreateQueueState(AYUFSBottleneckPoint* Bottleneck);
	int32 ReserveQueueIndex(AActor* Requester, FYUFSBottleneckQueueState& QueueState);
	bool HasPassedBottleneck(const AActor* Requester, const AYUFSBottleneckPoint* Bottleneck) const;
	FVector BuildQueueSlotLocation(const AYUFSBottleneckPoint* Bottleneck, int32 QueueIndex) const;
	FVector BuildPassThroughTarget(const AYUFSBottleneckPoint* Bottleneck) const;
	FVector ProjectToNavigation(const FVector& Location) const;
	float GetMinDistanceToPath2D(const FVector& Location, const TArray<FVector>& PathPoints) const;

	UPROPERTY()
	TArray<TObjectPtr<AYUFSBottleneckPoint>> CachedBottlenecks;

	UPROPERTY()
	TArray<FYUFSBottleneckQueueState> QueueStates;
};
