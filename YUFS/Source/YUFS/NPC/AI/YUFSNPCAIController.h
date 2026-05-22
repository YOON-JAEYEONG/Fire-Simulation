#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "YUFSNPCAIController.generated.h"

class UBehaviorTree;
class UBlackboardData;

// Blackboard 키 이름 상수 (Task/Service/Decorator에서 공유)
namespace YUFSBlackboardKeys
{
	static const FName BehaviorState    = TEXT("BehaviorState");
	static const FName TargetLocation   = TEXT("TargetLocation");
	static const FName bAlarmSounding   = TEXT("bAlarmSounding");
	static const FName bStaffGuidance   = TEXT("bStaffGuidance");
	static const FName bPreRecordedMsg  = TEXT("bPreRecordedMsg");
	static const FName bLiveAnnouncement= TEXT("bLiveAnnouncement");
	static const FName RiskPerception   = TEXT("RiskPerception");
	static const FName SmokeExposure    = TEXT("SmokeExposure");
	static const FName SmokeDensity     = TEXT("SmokeDensity");
	static const FName SmokeInFront     = TEXT("SmokeInFront");
	static const FName StressLevel      = TEXT("StressLevel");
	static const FName NearbyEvacRatio  = TEXT("NearbyEvacRatio");
	static const FName NearbyNPCCount   = TEXT("NearbyNPCCount");
	static const FName bNPCNeedsHelp    = TEXT("bNPCNeedsHelp");
	static const FName DistToNearestExit= TEXT("DistToNearestExit");
	static const FName DistToFamiliarExit=TEXT("DistToFamiliarExit");
	static const FName NearestExitLoc   = TEXT("NearestExitLoc");
	static const FName FamiliarExitLoc  = TEXT("FamiliarExitLoc");
	static const FName ShelterLoc       = TEXT("ShelterLoc");
	static const FName bNearestExitSafe = TEXT("bNearestExitSafe");
	static const FName StaffExitLoc     = TEXT("StaffExitLoc");
}

UCLASS()
class YUFS_API AYUFSNPCAIController : public AAIController
{
	GENERATED_BODY()

public:
	AYUFSNPCAIController();

	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

	UPROPERTY(EditDefaultsOnly, Category="AI")
	UBehaviorTree* BehaviorTreeAsset;
};
