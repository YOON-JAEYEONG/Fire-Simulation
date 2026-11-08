#include "NPC/Navigation/YUFSLocalMovementComponent.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "NavigationSystem.h"

UYUFSLocalMovementComponent::UYUFSLocalMovementComponent() { PrimaryComponentTick.bCanEverTick=false; }
FString UYUFSLocalMovementComponent::GetYieldingTo() const { return GetNameSafe(YieldingTo.Get()); }
FString UYUFSLocalMovementComponent::GetTrafficDiagnostics() const
{
	return FString::Printf(TEXT("state=%s yieldingTo=%s attempts=%d cooldown=%.2f recoveryTime=%.2f timeout=%.2f target=%s"),
		*StaticEnum<EYUFSLocalMovementState>()->GetNameStringByValue(int64(State)),*GetYieldingTo(),Attempts,Cooldown,RecoveryTime,RecoveryTimeout,*RecoveryTarget.ToString());
}
uint32 UYUFSLocalMovementComponent::GetTrafficPriorityId(const AYUFSEvacuationNPC* NPC)
{
	if (!NPC) return MAX_uint32;
	const int32 StableId = NPC->GetStableNPCId();
	return StableId == INDEX_NONE ? NPC->GetUniqueID() : static_cast<uint32>(StableId);
}
void UYUFSLocalMovementComponent::SetState(EYUFSLocalMovementState NewState)
{
	if (State==NewState) return;
	State=NewState;
	UE_LOG(LogTemp,Log,TEXT("[YUFS][Traffic] agent=%s state=%s yieldingTo=%s attempts=%d"),*GetNameSafe(GetOwner()),
		*StaticEnum<EYUFSLocalMovementState>()->GetNameStringByValue(int64(State)),*GetYieldingTo(),Attempts);
}
void UYUFSLocalMovementComponent::Reset()
{
	if (auto* NPC=Cast<AYUFSEvacuationNPC>(GetOwner())) NPC->GetCharacterMovement()->bUseRVOAvoidance=true;
	YieldingTo.Reset(); YieldTime=RecoveryTime=Cooldown=0.f; Attempts=0;
	SetState(EYUFSLocalMovementState::Following);
}
bool UYUFSLocalMovementComponent::TrajectoriesConflict(FVector A,FVector DA,float RA,FVector B,FVector DB,float RB,float LookAhead)
{
	if (FMath::Abs(A.Z-B.Z)>120.f) return false;
	A.Z=B.Z=0; DA=DA.GetSafeNormal2D(); DB=DB.GetSafeNormal2D();
	const FVector Relative=B-A, Velocity=(DB-DA)*LookAhead;
	const float T=Velocity.SizeSquared()>1.f ? FMath::Clamp(float(-FVector::DotProduct(Relative,Velocity)/Velocity.SizeSquared()),0.f,1.f) : 0.f;
	const float Gap=RA+RB+18.f;
	// Also form a following queue, before the capsules actually touch.
	const bool Following=FVector::DotProduct(DA,DB)>0.5f && FMath::Abs(FVector::DotProduct(Relative,DA))<Gap+85.f &&
		FMath::Abs(FVector::CrossProduct(Relative,DA).Z)<Gap;
	if (!Following && FVector::DotProduct(Relative,Velocity)>0.f) return false;
	return Following || (Relative+Velocity*T).SizeSquared()<FMath::Square(Gap);
}
bool UYUFSLocalMovementComponent::ShouldYieldTo(FVector A,FVector DA,uint32 IDA,FVector B,FVector DB,uint32 IDB)
{
	DA=DA.GetSafeNormal2D(); DB=DB.GetSafeNormal2D();
	if (FVector::DotProduct(DA,DB)>0.5f)
	{
		const float Ahead=FVector::DotProduct(B-A,(DA+DB).GetSafeNormal2D());
		if (FMath::Abs(Ahead)>25.f) return Ahead>0.f; // Never make the front agent wait for its follower.
	}
	return IDA>IDB; // Stable tie breaker prevents mirrored left/right decisions.
}
bool UYUFSLocalMovementComponent::IsCycleLeader(const AYUFSEvacuationNPC* Blocker) const
{
	const auto* Self = Cast<AYUFSEvacuationNPC>(GetOwner());
	if (!Self) return false;
	TSet<const AYUFSEvacuationNPC*> Visited;
	const AYUFSEvacuationNPC* Leader = Self;
	for (const AYUFSEvacuationNPC* Peer = Blocker; Peer; )
	{
		if (Peer == Self) return Leader == Self;
		if (Visited.Contains(Peer)) return false;
		Visited.Add(Peer);
		if (GetTrafficPriorityId(Peer) < GetTrafficPriorityId(Leader)) Leader = Peer;
		const auto* Traffic = Peer->FindComponentByClass<UYUFSLocalMovementComponent>();
		if (!Traffic || Traffic->State != EYUFSLocalMovementState::Yielding) return false;
		Peer = Traffic->YieldingTo.Get();
	}
	return false;
}
FVector UYUFSLocalMovementComponent::ResolveDirection(FVector Desired,float Dt,int32 Frame)
{
	auto* NPC=Cast<AYUFSEvacuationNPC>(GetOwner()); if (!NPC) return Desired;

	if (FVector::DistSquared2D(LastProgressPosition,NPC->GetActorLocation())>FMath::Square(180.f))
	{ Attempts=0; LastProgressPosition=NPC->GetActorLocation(); }
	AYUFSEvacuationNPC* Blocker=nullptr;
	const FVector A=NPC->GetActorLocation();
	const float Radius=NPC->GetCapsuleComponent()->GetScaledCapsuleRadius();
	float Nearest=FLT_MAX;
	FVector BlockerDirection=FVector::ZeroVector;
	bool bBlockerMoving=false;
	for (TActorIterator<AYUFSEvacuationNPC> It(GetWorld()); It; ++It)
	{
		auto* Other=*It;
		if (Other==NPC || Other->IsHidden() || !Other->GetActorEnableCollision()) continue;
		const FVector B=Other->GetActorLocation();
		const float Dist=FVector::DistSquared(A,B);
		if (Dist>FMath::Square(LookAheadCm*2.f) || FMath::Abs(A.Z-B.Z)>120.f) continue;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(YUFSTrafficSight),false,NPC); Params.AddIgnoredActor(Other);
		if (GetWorld()->LineTraceTestByChannel(A,B,ECC_Visibility,Params)) continue;
		const auto* Nav=Other->GetNavigator();
		const auto* PeerTraffic=Other->GetLocalMovement();
		const bool Moving=IsMovingPeer(Nav && Nav->IsFollowingPath(),Other->IsInteractionHoldingPosition(),
			PeerTraffic && PeerTraffic->IsDeliberatelyWaiting());
		const FVector OtherDir=Moving ? (Nav->GetSteeringTarget(B,120.f)-B).GetSafeNormal2D() : FVector::ZeroVector;
		if (!TrajectoriesConflict(A,Desired,Radius,B,OtherDir,Other->GetCapsuleComponent()->GetScaledCapsuleRadius(),LookAheadCm)) continue;
		// A stationary person is a real obstacle. Moving agents use a stable right of way.
		if (!Moving || ShouldYieldTo(A,Desired,GetTrafficPriorityId(NPC),B,OtherDir,GetTrafficPriorityId(Other)))
		{
			const bool bNearer = Dist < Nearest - 1.f;
			const bool bStableTie = FMath::IsNearlyEqual(Dist, Nearest, 1.f)
				&& (!Blocker || GetTrafficPriorityId(Other) < GetTrafficPriorityId(Blocker));
			if (!IsCycleLeader(Other) && (bNearer || bStableTie))
			{ Blocker=Other; Nearest=Dist; BlockerDirection=OtherDir; bBlockerMoving=Moving; }
		}
	}
	if (Blocker)
	{
		if (YieldingTo.Get()!=Blocker || FVector::DistSquared2D(LastBlockerPosition,Blocker->GetActorLocation())>FMath::Square(100.f))
		{
			YieldingTo=Blocker; YieldTime=0.f; Attempts=0; LastBlockerPosition=Blocker->GetActorLocation();
		}
		YieldTime+=Dt;
		// A following queue can keep moving while there is capsule clearance. Do not brake to zero far behind the leader.
		const float Ahead=FVector::DotProduct(Blocker->GetActorLocation()-A,Desired.GetSafeNormal2D());
		const float ContactGap=Radius+Blocker->GetCapsuleComponent()->GetScaledCapsuleRadius();
		const auto* PeerTraffic=Blocker->GetLocalMovement();const auto* PeerNav=Blocker->GetNavigator();
		const bool StationaryObstacle=Blocker->IsInteractionHoldingPosition()
			|| ((!PeerNav || !PeerNav->IsFollowingPath()) && (!PeerTraffic || !PeerTraffic->IsDeliberatelyWaiting()));
		const float BodyDistance=FVector::Dist2D(A,Blocker->GetActorLocation());
		if (StationaryObstacle && BodyDistance>ContactGap+45.f)
		{
			// Prediction can see a person through a doorway before there is room to
			// pass them. Approach slowly with capsule clearance instead of retreating
			// from 300cm away; ordinary character collision/RVO remains active.
			YieldTime=0.f;SetState(EYUFSLocalMovementState::Following);
			return Desired*FMath::Clamp((BodyDistance-ContactGap-45.f)/FMath::Max(1.f,LookAheadCm),.15f,.6f);
		}
		if (bBlockerMoving && FVector::DotProduct(Desired.GetSafeNormal2D(),BlockerDirection)>0.7f && Ahead>ContactGap+28.f)
		{
			YieldTime=0.f;
			SetState(EYUFSLocalMovementState::Following);
			return Desired*FMath::Clamp((Ahead-ContactGap)/120.f,0.2f,0.85f);
		}
		if (State!=EYUFSLocalMovementState::Blocked || Cooldown<=0.f && Attempts<MaxRecoveryAttempts)
			SetState(EYUFSLocalMovementState::Yielding);
		NPC->ConsumeMovementInputVector(); NPC->GetCharacterMovement()->StopMovementImmediately();
		// Yield first. If standing still blocks the winner too, make room by a checked physical back/side step.
		if (YieldTime>FMath::Max(MinYieldSeconds,0.3f) && Cooldown<=0.f &&
			(Nearest<FMath::Square(Radius+Blocker->GetCapsuleComponent()->GetScaledCapsuleRadius()+40.f) || YieldTime>3.f))
			TryRecovery(Desired,Frame);
		return FVector::ZeroVector;
	}
	// The other agent has cleared the conflict; don't keep an expired reservation of the doorway.
	YieldingTo.Reset(); YieldTime=0.f;
	SetState(EYUFSLocalMovementState::Following);
	return Desired;
}
bool UYUFSLocalMovementComponent::IsCandidateReachable(FVector CandidateFeet,int32 Frame,FVector& ProjectedFeet) const
{
	const auto* NPC=Cast<AYUFSEvacuationNPC>(GetOwner()); if (!NPC || !GetWorld()) return false;
	const auto* Cap=NPC->GetCapsuleComponent(); const float Half=Cap->GetScaledCapsuleHalfHeight();
	const FVector StartFeet=NPC->GetActorLocation()-FVector(0,0,Half);
	auto* NavSys=UNavigationSystemV1::GetCurrent(GetWorld()); if (!NavSys) return false;
	const auto* NavData=NavSys->GetNavDataForProps(NPC->GetNavAgentPropertiesRef(),StartFeet); if (!NavData) return false;
	FNavLocation Projected;
	const auto* Movement=NPC->GetCharacterMovement();
	const float ReachableRise=Movement->MaxStepHeight+FVector::Dist2D(StartFeet,CandidateFeet)*FMath::Tan(FMath::DegreesToRadians(Movement->GetWalkableFloorAngle()));
	// Search the stair's actual surface height; the continuous physical test below rejects floor shortcuts.
	if (!NavSys->ProjectPointToNavigation(CandidateFeet,Projected,FVector(45,45,FMath::Max(45.f,ReachableRise)),NavData)) return false;
	ProjectedFeet=Projected.Location;
	if (FVector::DistSquared2D(CandidateFeet,ProjectedFeet)>FMath::Square(45.f)) return false;
	return IsPhysicalCorridorClear(ProjectedFeet) && NPC->GetNavigator() &&
		NPC->GetNavigator()->IsLocalRecoverySafe(StartFeet,ProjectedFeet,Frame);
}
bool UYUFSLocalMovementComponent::IsPhysicalCorridorClear(FVector TargetFeet,bool bIgnorePeerNPCs,FVector* OutSupportedFloor) const
{
	const auto* NPC=Cast<AYUFSEvacuationNPC>(GetOwner());if (!NPC) return false;
	return IsPhysicalCorridorClearFrom(NPC->GetActorLocation()-FVector(0,0,NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),TargetFeet,bIgnorePeerNPCs,OutSupportedFloor);
}
bool UYUFSLocalMovementComponent::IsPhysicalCorridorClearFrom(FVector StartFeet,FVector TargetFeet,bool bIgnorePeerNPCs,FVector* OutSupportedFloor) const
{
	const auto* NPC=Cast<AYUFSEvacuationNPC>(GetOwner()); if (!NPC || !GetWorld()) return false;
	const auto* Cap=NPC->GetCapsuleComponent(); const float Half=Cap->GetScaledCapsuleHalfHeight();
	const auto* Movement=NPC->GetCharacterMovement();
	const float Step=Movement->MaxStepHeight;
	const float Distance=FVector::Dist2D(StartFeet,TargetFeet);
	const float MaxRise=Step+Distance*FMath::Tan(FMath::DegreesToRadians(Movement->GetWalkableFloorAngle()));
	if (TargetFeet.ContainsNaN() || FMath::Abs(TargetFeet.Z-StartFeet.Z)>MaxRise) return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(YUFSRecoverySweep),false,NPC);
	if (bIgnorePeerNPCs)
		for (TActorIterator<AYUFSEvacuationNPC> It(GetWorld()); It; ++It) Params.AddIgnoredActor(*It);
	const FCollisionShape Shape=FCollisionShape::MakeCapsule(FMath::Max(5.f,Cap->GetScaledCapsuleRadius()),FMath::Max(5.f,Half-2.f));
	FHitResult Hit;
	// Continuous floor support prevents stepping into a stairwell void or onto another storey.
	const int32 Count=FMath::Max(1,FMath::CeilToInt(Distance/30.f));
	float PreviousFloor=StartFeet.Z;
	FVector Center=StartFeet+FVector(0,0,Half+2);
	auto Sweep=[&](FVector From,FVector To,FHitResult& Result)
	{
		return GetWorld()->SweepSingleByProfile(Result,From,To,FQuat::Identity,Cap->GetCollisionProfileName(),Shape,Params);
	};
	for (int32 I=1; I<=Count; ++I)
	{
		const FVector P=FMath::Lerp(StartFeet,TargetFeet,float(I)/Count);
		if (!GetWorld()->LineTraceSingleByChannel(Hit,P+FVector(0,0,Step+12.f),P-FVector(0,0,Step+12.f),ECC_Visibility,Params)
			|| Hit.ImpactNormal.Z<Movement->GetWalkableFloorZ() || FMath::Abs(Hit.ImpactPoint.Z-PreviousFloor)>Step+3.f) return false;
		PreviousFloor=Hit.ImpactPoint.Z;
		const FVector DesiredCenter(P.X,P.Y,PreviousFloor+Half+2.f);
		if (!Sweep(Center,DesiredCenter,Hit)) { Center=DesiredCenter; continue; }
		if (Hit.bStartPenetrating) return false;
		// Validate a character-style step-up with full capsule clearance; queries never move the actor.
		const FVector Raised=Center+FVector(0,0,Step);
		const FVector Across(DesiredCenter.X,DesiredCenter.Y,Raised.Z);
		if (Sweep(Center,Raised,Hit) || Sweep(Raised,Across,Hit)) return false;
		if (Sweep(Across,DesiredCenter,Hit))
		{
			if (Hit.bStartPenetrating || Hit.ImpactNormal.Z<Movement->GetWalkableFloorZ()) return false;
			Center=Hit.Location+FVector(0,0,2);
		}
		else Center=DesiredCenter;
	}
	if (GetWorld()->OverlapBlockingTestByProfile(Center,FQuat::Identity,Cap->GetCollisionProfileName(),Shape,Params)) return false;
	if (OutSupportedFloor) *OutSupportedFloor=FVector(Center.X,Center.Y,PreviousFloor);
	return true;
}
int32 UYUFSLocalMovementComponent::SelectRecoverySpace(const TArray<FVector>& Candidates,FVector Desired) const
{
	if (Candidates.IsEmpty()) return INDEX_NONE;
	const auto* NPC=Cast<AYUFSEvacuationNPC>(GetOwner());if (!NPC) return 0;
	float ContinuationCm=180.f;
	const auto* Peer=YieldingTo.Get();FVector PeerDirection=FVector::ZeroVector;
	// Keep the moving queue's right of way. Someone operating an object or standing
	// without a route will not clear that reservation just because we keep backing up.
	if (Peer)
	{
		const auto* PeerTraffic=Peer->GetLocalMovement();
		const auto* PeerNav=Peer->GetNavigator();
		const bool TemporaryQueue=PeerTraffic && PeerTraffic->IsDeliberatelyWaiting();
		const bool HasRoute=PeerNav && PeerNav->IsFollowingPath();
		if (HasRoute) PeerDirection=(PeerNav->GetSteeringTarget(Peer->GetActorLocation(),120.f)-Peer->GetActorLocation()).GetSafeNormal2D();
		if (!Peer->IsInteractionHoldingPosition())
		{
			if (TemporaryQueue && PeerDirection.IsNearlyZero()) return 0;
			if (FVector::DotProduct(Desired.GetSafeNormal2D(),PeerDirection)>.5f && (TemporaryQueue || Peer->GetVelocity().SizeSquared2D()>25.f)) return 0;
		}
		if (Peer->IsInteractionHoldingPosition() || !HasRoute) PeerDirection=FVector::ZeroVector;
		// Predictive traffic yields before body contact. A short fixed probe could
		// end before the blocker and incorrectly rate every backwards step as clear.
		const float BodyClearance=NPC->GetCapsuleComponent()->GetScaledCapsuleRadius()+Peer->GetCapsuleComponent()->GetScaledCapsuleRadius()+20.f;
		ContinuationCm=FMath::Clamp(float(FVector::Dist2D(NPC->GetActorLocation(),Peer->GetActorLocation()))+BodyClearance,180.f,700.f);
	}
	// All supplied candidates already passed navigation, physical support and personal
	// hazard checks. Prefer an onward corridor; peer body collision is included too.
	const FVector Feet=NPC->GetActorLocation()-FVector(0,0,NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	int32 Best=0;float BestScore=-FLT_MAX;
	for (int32 I=0;I<Candidates.Num();++I)
	{
		const bool Clear=IsPhysicalCorridorClearFrom(Candidates[I],Candidates[I]+Desired.GetSafeNormal2D()*ContinuationCm,false);
		// A wall farther along the old heading must not hide a useful passing bay:
		// candidate safety is checked before this preference and the planner replans there.
		const FVector CandidateCenter=Candidates[I]+FVector(0,0,NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		const bool ClearsPeer=Peer && !TrajectoriesConflict(CandidateCenter,Desired,NPC->GetCapsuleComponent()->GetScaledCapsuleRadius(),
			Peer->GetActorLocation(),PeerDirection,Peer->GetCapsuleComponent()->GetScaledCapsuleRadius(),ContinuationCm);
		const float PassingProgress=ClearsPeer ? float(FVector::DotProduct(Candidates[I]-Feet,Desired.GetSafeNormal2D()))*2.f : 0.f;
		const float Score=(ClearsPeer?2000.f:0.f)+(Clear?1000.f:0.f)+PassingProgress-FVector::Dist2D(Feet,Candidates[I]);
		if (Score>BestScore+1.f) { BestScore=Score;Best=I; } // Preserve candidate order across sub-centimetre projection noise.
	}
	return Best;
}
bool UYUFSLocalMovementComponent::TryRecovery(FVector Desired,int32 Frame)
{
	auto* NPC=Cast<AYUFSEvacuationNPC>(GetOwner()); if (!NPC || IsRecovering() || Cooldown>0.f) return false;
	const bool Exhausted=Attempts>=FMath::Max(1,MaxRecoveryAttempts);
	if (!Exhausted) ++Attempts;
	Cooldown=Exhausted ? FMath::Max(1.f,BlockedRecheckSeconds) : FMath::Max(0.2f,RecoveryCooldownSeconds);
	Desired=Desired.GetSafeNormal2D(); if (Desired.IsNearlyZero()) Desired=NPC->GetActorForwardVector();
	if (!YieldingTo.IsValid())
	{
		FHitResult Hit;const auto* Capsule=NPC->GetCapsuleComponent();
		const FVector Start=NPC->GetActorLocation()+FVector(0,0,2);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(YUFSStuckObstacle),false,NPC);
		if (GetWorld()->SweepSingleByProfile(Hit,Start,Start+Desired*180.f,FQuat::Identity,Capsule->GetCollisionProfileName(),
			FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),FMath::Max(5.f,Capsule->GetScaledCapsuleHalfHeight()-2.f)),Params))
			UE_LOG(LogTemp,Log,TEXT("[YUFS][Traffic] agent=%s blockedBy=%s component=%s"),*NPC->GetName(),*GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()));
	}
	const FVector Back=-Desired, Side=FVector(-Desired.Y,Desired.X,0)*(GetTrafficPriorityId(NPC)%2 ? 1.f : -1.f);
	const FVector Feet=NPC->GetActorLocation()-FVector(0,0,NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	TArray<FVector> Candidates;
	for (float Distance:{90.f,150.f,210.f}) for (FVector D:{Back,(Back+Side).GetSafeNormal(),(Back-Side).GetSafeNormal(),Side,-Side,
		(Desired+Side).GetSafeNormal(),(Desired-Side).GetSafeNormal()})
	{
		FVector P;
		if (!IsCandidateReachable(Feet+D*Distance,Frame,P) || FVector::DistSquared2D(P,Feet)<FMath::Square(50.f)) continue;
		Candidates.Add(P);
	}
	const int32 Selected=SelectRecoverySpace(Candidates,Desired);
	if (Selected!=INDEX_NONE)
	{
		const FVector P=Candidates[Selected];
		// An exhausted batch only resumes after a candidate passes the current space/hazard checks.
		if (Exhausted) Attempts=1;
		RecoveryTarget=P; RecoveryTime=0.f;
		const float RecoverySpeed=FMath::Max(20.f,FMath::Min(NPC->GetCharacterMovement()->MaxWalkSpeed,160.f)*0.45f);
		RecoveryTimeout=FMath::Clamp(FVector::Dist2D(P,Feet)/RecoverySpeed+1.f,2.f,8.f);
		NPC->ConsumeMovementInputVector(); NPC->GetCharacterMovement()->StopMovementImmediately();
		NPC->GetCharacterMovement()->bUseRVOAvoidance=false;
		SetState(EYUFSLocalMovementState::Recovering); return true;
	}
	SetState(EYUFSLocalMovementState::Blocked); return false;
}
bool UYUFSLocalMovementComponent::TickRecovery(float Dt,int32 Frame)
{
	if (!IsRecovering()) { Cooldown=FMath::Max(0.f,Cooldown-Dt); return false; }
	auto* NPC=Cast<AYUFSEvacuationNPC>(GetOwner()); if (!NPC) return false;
	RecoveryTime+=Dt;
	FVector Checked;
	const bool Arrived=FVector::DistSquared2D(NPC->GetActorLocation(),RecoveryTarget)<FMath::Square(20.f);
	if (Arrived || RecoveryTime>RecoveryTimeout || !IsCandidateReachable(RecoveryTarget,Frame,Checked))
	{
		NPC->ConsumeMovementInputVector(); NPC->GetCharacterMovement()->StopMovementImmediately();
		NPC->GetCharacterMovement()->bUseRVOAvoidance=true;
		YieldingTo.Reset(); YieldTime=0.f; Cooldown=RecoveryCooldownSeconds;
		SetState(Arrived ? EYUFSLocalMovementState::Following : EYUFSLocalMovementState::Blocked);
		if (NPC->GetNavigator()) NPC->GetNavigator()->ReplanPath(Frame,EYUFSRepathReason::Stuck);
		return true;
	}
	NPC->AddMovementInput((RecoveryTarget-NPC->GetActorLocation()).GetSafeNormal2D(),0.45f);
	return true;
}
