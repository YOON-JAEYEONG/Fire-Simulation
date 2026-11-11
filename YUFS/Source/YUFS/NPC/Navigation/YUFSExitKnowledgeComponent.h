#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Fire/YUFSHazardField.h"
#include "YUFSExitKnowledgeComponent.generated.h"

class AYUFSExitPoint;
class AYUFSEvacuationNPC;
class AYUFSLevelDataManager;

struct FYUFSRememberedExit
{
	TWeakObjectPtr<AYUFSExitPoint> Actor;
	FVector Location=FVector::ZeroVector;
	FName Source;
	bool bFamiliar=false;
};

UCLASS(ClassGroup=(YUFS),meta=(BlueprintSpawnableComponent))
class YUFS_API UYUFSExitKnowledgeComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UYUFSExitKnowledgeComponent();
	// These are authored prior knowledge, not an automatic nearest exit/all-exits query.
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Exit Knowledge") bool bRememberMarkedEntry=true;
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Exit Knowledge") FName FamiliarEntryExitID;
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Exit Knowledge") TArray<FName> InitiallyKnownExitIDs;
	UPROPERTY(EditAnywhere,Category="Exit Knowledge") float ObservationIntervalSeconds=.5f;
	UPROPERTY(EditAnywhere,Category="Exit Knowledge") float RecognitionHeightCm=140.f;
	UPROPERTY(EditAnywhere,Category="Exit Search") float SearchRadiusCm=450.f;
	UPROPERTY(EditAnywhere,Category="Exit Search") float MaxSearchLegSeconds=15.f;
	UPROPERTY(EditAnywhere,Category="Exit Search") float SearchWalkingSpeed=160.f;
	void InitializeMemory();
	void ResetMemory();
	void UpdateKnowledge(float DeltaTime);
	void ObserveExits(const FYUFSHazardSnapshot& Snapshot,bool bSmokeActive);
	bool RememberGuidedExit(FVector Location,FName Source);
	TArray<FVector> GetKnownExitLocations() const;
	bool KnowsExitLocation(FVector Location) const;
	bool TryGetFamiliarExit(FVector& Out) const;
	bool TryGetSearchTarget(FVector& Out);
	void StopSearching();
	void TickSearchLook(float DeltaTime);
	UFUNCTION(BlueprintPure) int32 GetKnownExitCount() const;
	UFUNCTION(BlueprintPure) bool IsSearchingForExit() const { return bSearching; }
	UFUNCTION(BlueprintPure) bool HasSearchTarget() const { return bHasSearchTarget; }
	UFUNCTION(BlueprintPure) int32 GetSearchQueryCount() const { return SearchQueryCount; }
	UFUNCTION(BlueprintPure) FString GetKnowledgeDiagnostics() const;
private:
	AYUFSEvacuationNPC* OwnerNPC() const;
	AYUFSLevelDataManager* Manager() const;
	void Remember(AYUFSExitPoint* Exit,FName Source,bool bFamiliar=false);
	void Visit(FVector Point);
	TArray<FYUFSRememberedExit> Exits;
	TArray<FVector> Visited;
	FRandomStream SearchRandom;
	bool bInitialized=false,bSearching=false,bHasSearchTarget=false;
	float Clock=0.f,ObservationTimer=0.f,NextSearchAt=0.f,SearchDeadline=0.f;
	FVector SearchTarget=FVector::ZeroVector;
	int32 SearchQueryCount=0;
};
