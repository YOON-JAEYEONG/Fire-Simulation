#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "Core/YUFSObservation.h"
#include "Misc/Crc.h"

UYUFSBehaviorStateMachine::UYUFSBehaviorStateMachine() { PrimaryComponentTick.bCanEverTick = false; }
void UYUFSBehaviorStateMachine::BeginPlay()
{
	Super::BeginPlay();
	if (!Config) Config = NewObject<UYUFSBehaviorConfig>(this);
	// Stable for the same spawn positions and experiment seed. Never mutate a shared DataAsset.
	const FVector P = GetOwner()->GetActorLocation();
	const FString Key = FString::Printf(TEXT("%d/%d/%d"),FMath::RoundToInt(P.X),FMath::RoundToInt(P.Y),FMath::RoundToInt(P.Z));
	InitializePersonality(int32(FCrc::StrCrc32(*Key)) ^ Config->PersonalitySeed);
}
void UYUFSBehaviorStateMachine::InitializePersonality(int32 Seed)
{
	FRandomStream R(Seed);
	AlarmTrust=R.FRand(); Sensitivity=R.FRandRange(0.75f,1.3f); ResponseDelay=R.FRandRange(0.8f,3.5f);
	AlarmDecisionTime = AlarmTrust > 1.f-FMath::Clamp(Config ? Config->EarlyAlarmResponseFraction : 0.3f,0.f,1.f)
		? R.FRandRange(3.f,9.f) : R.FRandRange(18.f,45.f);
	PreparationScale=R.FRandRange(0.3f,1.2f); SpeedMultiplier=R.FRandRange(0.85f,1.12f); RoutePreference=R.FRand();
}
void UYUFSBehaviorStateMachine::TickStateMachine(float Dt, const FYUFSNPCObservation& Obs)
{
	if (!Config) return;
	Dt=FMath::Max(0.f,Dt);
	const auto Previous=CurrentState;
	StateTimer+=Dt; DirectEvidenceAge+=Dt;
	if (Obs.bAlarmSounding) AlarmElapsed+=Dt;
	AccumulateRiskPerception(Obs,Dt);
	AccumulateSmokeExposure(Obs,Dt);
	TryTransition(Obs);
	if (Previous!=CurrentState)
	{
		StateTimer=0.f;
		UE_LOG(LogTemp,Log,TEXT("[YUFS][Decision] agent=%s state=%s cue=%s trust=%.2f risk=%.2f smoke=%.2f heat=%.2f"),
			*GetNameSafe(GetOwner()),*StaticEnum<EYUFSBehaviorState>()->GetNameStringByValue(int64(CurrentState)),
			*StaticEnum<EYUFSEvacuationCue>()->GetNameStringByValue(int64(DecisionCue)),AlarmTrust,RiskPerception,
			FMath::Max(Obs.SmokeInFrontNormalized,Obs.SmokeDensityAtSelf),FMath::Max(Obs.NearbyHeat,Obs.HeatInSight));
	}
}
void UYUFSBehaviorStateMachine::AccumulateRiskPerception(const FYUFSNPCObservation& O,float Dt)
{
	const float Smoke=FMath::Max3(O.SmokeDensityAtSelf,O.SmokeInFrontNormalized,O.SmokeAboveNormalized);
	const float Heat=FMath::Max3(O.TemperatureAtSelf,O.NearbyHeat,O.HeatInSight);
	const bool DirectSmoke=Smoke*Sensitivity > Config->SmokeAwarenessThreshold;
	const bool DirectHeat=Heat*Sensitivity > Config->HeatAwarenessThreshold;
	const bool Crowd=O.NearbyNPCCount>0 && O.NearbyEvacuatingRatio>0.3f;
	const bool Guidance=O.bReceivedStaffGuidance || O.bReceivedLiveAnnouncement || O.bReceivedPreRecordedMsg;
	if (DirectSmoke || DirectHeat)
	{
		DirectEvidenceAge=0.f;
		DecisionCue=DirectHeat ? EYUFSEvacuationCue::Heat : EYUFSEvacuationCue::Smoke;
	}
	else if (!bCommitted)
	{
		if (Guidance) DecisionCue=EYUFSEvacuationCue::Guidance;
		else if (O.bHeardPeerWarning) DecisionCue=EYUFSEvacuationCue::PeerWarning;
		else if (Crowd) DecisionCue=EYUFSEvacuationCue::Crowd;
		else if (DirectEvidenceAge > Config->EvidenceMemorySeconds) DecisionCue=O.bAlarmSounding ? EYUFSEvacuationCue::Alarm : EYUFSEvacuationCue::None;
	}
	const bool Corroborated=DirectEvidenceAge < Config->EvidenceMemorySeconds || O.bHeardPeerWarning || Crowd || Guidance;
	if (Corroborated)
	{
		EvidenceTime+=Dt;
		RiskPerception=FMath::Clamp(RiskPerception+Dt*Sensitivity*(0.08f+0.25f*FMath::Max(Smoke,Heat)),0.f,1.f);
	}
	else
	{
		EvidenceTime=0.f;
		// Alarm alone has a limited contribution; it cannot accumulate into certainty for every agent.
		const float AlarmRisk=O.bAlarmSounding ? 0.12f*AlarmTrust : 0.f;
		RiskPerception=FMath::FInterpConstantTo(RiskPerception,AlarmRisk,Dt,0.05f);
	}
}
bool UYUFSBehaviorStateMachine::CheckEmergencyOverride(const FYUFSNPCObservation& O) const
{
	return O.SmokeDensityAtSelf > Config->SmokeAwarenessThreshold*Config->EmergencyOverrideMultiplier ||
		FMath::Max(O.TemperatureAtSelf,O.NearbyHeat) >= Config->EmergencyHeatThreshold;
}
void UYUFSBehaviorStateMachine::TryTransition(const FYUFSNPCObservation& O)
{
	if (CurrentState==EYUFSBehaviorState::Incapacitated) return;
	if (SmokeExposureAccumulated>=Config->IncapacitationThreshold) { CurrentState=EYUFSBehaviorState::Incapacitated; return; }
	if (SmokeExposureAccumulated>=Config->CrawlThreshold) { bCommitted=true; CurrentState=EYUFSBehaviorState::Crawling; return; }
	if (CheckEmergencyOverride(O))
	{
		bCommitted=true; CurrentState=EYUFSBehaviorState::Evacuating;
		DecisionCue=FMath::Max(O.TemperatureAtSelf,O.NearbyHeat)>=Config->EmergencyHeatThreshold ? EYUFSEvacuationCue::Heat : EYUFSEvacuationCue::Smoke;
		return;
	}
	if (CurrentState==EYUFSBehaviorState::Crawling) { CurrentState=EYUFSBehaviorState::Evacuating; return; }
	const bool StrongSight=FMath::Max3(O.HeatInSight,O.SmokeInFrontNormalized,O.SmokeAboveNormalized)>0.55f;
	const bool EvidenceReady=EvidenceTime >= (StrongSight ? 0.6f : ResponseDelay) &&
		(RiskPerception>=Config->RiskPerceptionThreshold/Sensitivity || StrongSight || O.bReceivedStaffGuidance);
	const float AlarmFraction=FMath::Clamp(Config->EarlyAlarmResponseFraction+Config->VerifyAlarmResponseFraction,0.f,1.f);
	const bool AlarmReady=O.bAlarmSounding && AlarmTrust>1.f-AlarmFraction && AlarmElapsed>=AlarmDecisionTime;
	if (!bCommitted && (EvidenceReady || AlarmReady))
	{
		bCommitted=true;
		if (!EvidenceReady) DecisionCue=EYUFSEvacuationCue::Alarm;
		CurrentState=StrongSight || O.bReceivedStaffGuidance ? EYUFSBehaviorState::Evacuating : EYUFSBehaviorState::Preparing;
		return;
	}
	switch(CurrentState)
	{
	case EYUFSBehaviorState::Normal:
		if (DecisionCue!=EYUFSEvacuationCue::None) CurrentState=EYUFSBehaviorState::Perceiving;
		break;
	case EYUFSBehaviorState::Perceiving:
		if (StateTimer>=ResponseDelay) CurrentState=EYUFSBehaviorState::Milling;
		break;
	case EYUFSBehaviorState::Milling:
		// No universal timeout that silently forces sceptical agents to evacuate.
		if (DecisionCue==EYUFSEvacuationCue::None) CurrentState=EYUFSBehaviorState::Normal;
		break;
	case EYUFSBehaviorState::Preparing:
		if (StateTimer>=Config->PreparationDuration*PreparationScale) CurrentState=EYUFSBehaviorState::Evacuating;
		break;
	case EYUFSBehaviorState::Helping:
		if (!O.bNearbyNPCNeedsHelp || StateTimer>Config->MaxHelpingDuration) CurrentState=EYUFSBehaviorState::Evacuating;
		break;
	case EYUFSBehaviorState::Evacuating:
		if (O.bNearbyNPCNeedsHelp && RiskPerception<Config->RiskPerceptionThreshold*0.7f) CurrentState=EYUFSBehaviorState::Helping;
		break;
	default: break;
	}
}
void UYUFSBehaviorStateMachine::AccumulateSmokeExposure(const FYUFSNPCObservation& Obs, float DeltaTime)
{
	if (!Config) return;

	// 연기가 있을 때만 누적. 연기 농도에 비례하여 더 빠르게 누적.
	if (Obs.SmokeDensityAtSelf > 0.f)
	{
		const float ExposureRate = Config->SmokeExposureAccumRate * Obs.SmokeDensityAtSelf;
		SmokeExposureAccumulated = FMath::Clamp(SmokeExposureAccumulated + ExposureRate * DeltaTime, 0.f, 1.f);
	}
	// 연기가 없는 곳에서는 아주 천천히 회복 (신선한 공기 찾는 중)
	else
	{
		const float RecoveryRate = Config->SmokeExposureAccumRate * 0.1f;
		SmokeExposureAccumulated = FMath::Clamp(SmokeExposureAccumulated - RecoveryRate * DeltaTime, 0.f, 1.f);
	}
}


void UYUFSBehaviorStateMachine::OnAlarmReceived() { /* Observation records the cue; no shared immediate risk jump. */ }
void UYUFSBehaviorStateMachine::OnPreRecordedMessageReceived() {}
void UYUFSBehaviorStateMachine::OnStaffGuidanceReceived() {}
void UYUFSBehaviorStateMachine::OnLiveAnnouncementReceived() {}
