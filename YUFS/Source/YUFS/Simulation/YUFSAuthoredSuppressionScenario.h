#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "YUFSAuthoredSuppressionScenario.generated.h"

/** Authored gesture scenario, not FDS evidence or a calibrated behavior sample. */
UCLASS(Config=Game)
class YUFS_API AYUFSAuthoredSuppressionScenario : public AActor
{
	GENERATED_BODY()
public:
	AYUFSAuthoredSuppressionScenario();
	virtual void Tick(float DeltaTime) override;
	UPROPERTY(Config, EditAnywhere, Category="Scenario") bool bEnabled = false;
	UPROPERTY(Config, EditAnywhere, Category="Scenario") FString MapName = TEXT("Prototype");
	UPROPERTY(Config, EditAnywhere, Category="Scenario", meta=(MakeEditWidget=true))
	FVector TargetWorldLocation = FVector::ZeroVector;
	UPROPERTY(Config, EditAnywhere, Category="Scenario", meta=(ClampMin="1", ClampMax="30"))
	float SprayDurationSeconds = 8.f;
	bool HasStarted() const { return bStarted; }
private:
	bool bStarted = false;
	float ScanElapsed = 0.f;
	int32 UnavailableScans = 0;
};
