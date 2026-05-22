#include "NPC/AI/YUFSBT_Service_UpdateBlackboard.h"
#include "NPC/AI/YUFSNPCAIController.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "NPC/Perception/YUFSNPCPerceptionComponent.h"
#include "NPC/Social/YUFSSocialInfluenceComponent.h"
#include "Level/YUFSLevelDataManager.h"
#include "Fire/YUFSBinaryManager.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "EngineUtils.h"

UYUFSBT_Service_UpdateBlackboard::UYUFSBT_Service_UpdateBlackboard()
{
	NodeName = TEXT("Update Blackboard (YUFS)");
	Interval = 0.1f;
	RandomDeviation = 0.02f;
}

void UYUFSBT_Service_UpdateBlackboard::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	AYUFSNPCAIController* Controller = Cast<AYUFSNPCAIController>(OwnerComp.GetAIOwner());
	if (!Controller) return;

	AYUFSEvacuationNPC* NPC = Cast<AYUFSEvacuationNPC>(Controller->GetPawn());
	if (!NPC) return;

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) return;

	// ── PADM 상태 ───────────────────────────────────────────────────────
	UYUFSBehaviorStateMachine* SM = NPC->GetBehaviorStateMachine();
	if (SM)
	{
		BB->SetValueAsInt(YUFSBlackboardKeys::BehaviorState,
			static_cast<int32>(SM->GetCurrentState()));
		BB->SetValueAsFloat(YUFSBlackboardKeys::RiskPerception, SM->GetRiskPerception());
		BB->SetValueAsFloat(YUFSBlackboardKeys::SmokeExposure,  SM->GetSmokeExposure());
	}

	// ── 지각 ─────────────────────────────────────────────────────────────
	UYUFSNPCPerceptionComponent* Perc = NPC->GetNPCPerceptionComponent();
	if (Perc)
	{
		BB->SetValueAsFloat(YUFSBlackboardKeys::SmokeDensity, Perc->GetSmokeDensity());
		BB->SetValueAsFloat(YUFSBlackboardKeys::SmokeInFront, Perc->GetSmokeInFrontNormalized());
		BB->SetValueAsFloat(YUFSBlackboardKeys::StressLevel,   Perc->GetRiskLevel());
	}

	// ── 사회적 영향 ───────────────────────────────────────────────────────
	UYUFSSocialInfluenceComponent* Social = NPC->GetSocialComponent();
	if (Social)
	{
		BB->SetValueAsFloat(YUFSBlackboardKeys::NearbyEvacRatio, Social->GetNearbyEvacuatingRatio());
		BB->SetValueAsInt  (YUFSBlackboardKeys::NearbyNPCCount,  Social->GetNearbyNPCCount());
		BB->SetValueAsBool (YUFSBlackboardKeys::bNPCNeedsHelp,   Social->ShouldHelpNearbyNPC());
	}

	// ── 통신 상태 ──────────────────────────────────────────────────────────
	BB->SetValueAsBool(YUFSBlackboardKeys::bAlarmSounding,    NPC->IsAlarmSounding());
	BB->SetValueAsBool(YUFSBlackboardKeys::bStaffGuidance,    NPC->HasReceivedStaffGuidance());
	BB->SetValueAsBool(YUFSBlackboardKeys::bPreRecordedMsg,   NPC->HasReceivedPreRecordedMsg());
	BB->SetValueAsBool(YUFSBlackboardKeys::bLiveAnnouncement, NPC->HasReceivedLiveAnnouncement());

	// ── 위치/출구 정보 ────────────────────────────────────────────────────
	AYUFSLevelDataManager* LDM = NPC->GetLevelDataManager();
	AYUFSBinaryManager*    BM  = NPC->GetBinaryManager();
	if (LDM)
	{
		const FVector Pos  = NPC->GetActorLocation();
		const int32 Frame  = BM ? BM->GetCurrentFrame() : 0;

		const FVector NearestExit  = LDM->GetNearestSafeExit(Pos, true, Frame);
		const FVector FamiliarExit = LDM->GetFamiliarExit(NPC->GetSpawnLocation());
		const FVector Shelter      = LDM->GetNearestAvailableShelter(Pos);

		BB->SetValueAsVector(YUFSBlackboardKeys::NearestExitLoc,    NearestExit);
		BB->SetValueAsVector(YUFSBlackboardKeys::FamiliarExitLoc,   FamiliarExit);
		BB->SetValueAsVector(YUFSBlackboardKeys::ShelterLoc,        Shelter);
		BB->SetValueAsFloat (YUFSBlackboardKeys::DistToNearestExit,  FVector::Dist(Pos, NearestExit));
		BB->SetValueAsFloat (YUFSBlackboardKeys::DistToFamiliarExit, FVector::Dist(Pos, FamiliarExit));
		BB->SetValueAsBool  (YUFSBlackboardKeys::bNearestExitSafe,   !LDM->IsLocationDangerous(NearestExit, Frame));
		BB->SetValueAsVector(YUFSBlackboardKeys::StaffExitLoc,       NPC->GetStaffGuidedExitLocation());
	}
}
