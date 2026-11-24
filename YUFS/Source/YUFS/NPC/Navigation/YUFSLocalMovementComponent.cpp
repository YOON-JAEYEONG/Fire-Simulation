#include "NPC/Navigation/YUFSLocalMovementComponent.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "Fire/YUFSInteractionDoor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
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
	DoorwayFollowThrough.Reset(); DoorwayRecoveryDoor.Reset(); bDoorwayRecovery=false;
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
	const double DoorLookAhead=FMath::Clamp(NPC->GetVelocity().Size2D()*.25+Radius, 60.0, 180.0);
	const FVector DoorApproach=A+Desired.GetSafeNormal2D()*DoorLookAhead;
	for (TActorIterator<AYUFSInteractionDoor> It(GetWorld()); It; ++It)
	{
		const auto* Door=*It;
		if (!Door->ConflictsWithOpening(*NPC,A,DoorApproach)) continue;
		YieldingTo.Reset();
		NPC->ConsumeMovementInputVector();
		NPC->GetCharacterMovement()->StopMovementImmediately();
		if (!TryRecovery(Desired, Frame))
			SetState(EYUFSLocalMovementState::Yielding);
		return FVector::ZeroVector;
	}
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
bool UYUFSLocalMovementComponent::IsCandidateReachable(FVector CandidateFeet,int32 Frame,FVector& ProjectedFeet,
	const AYUFSInteractionDoor* ClearanceDoor) const
{
	const auto* NPC=Cast<AYUFSEvacuationNPC>(GetOwner()); if (!NPC || !GetWorld()) return false;
	const auto* Cap=NPC->GetCapsuleComponent(); const float Half=Cap->GetScaledCapsuleHalfHeight();
	const FVector StartFeet=NPC->GetActorLocation()-FVector(0,0,Half);
	auto* NavSys=UNavigationSystemV1::GetCurrent(GetWorld()); if (!NavSys) return false;
	const auto* NavData=NavSys->GetNavDataForProps(NPC->GetNavAgentPropertiesRef(),StartFeet);
	FNavLocation Projected;
	const auto* Movement=NPC->GetCharacterMovement();
	const float ReachableRise=Movement->MaxStepHeight+FVector::Dist2D(StartFeet,CandidateFeet)*FMath::Tan(FMath::DegreesToRadians(Movement->GetWalkableFloorAngle()));
	// Search the stair's actual surface height; the continuous physical test below rejects floor shortcuts.
	auto CheckStep=[&](FVector EndFeet)
	{
		const FVector Start=NPC->GetActorLocation(), Destination=EndFeet+FVector(0,0,Half);
		for (TActorIterator<AYUFSInteractionDoor> Door(GetWorld()); Door; ++Door)
		{
			if (*Door==ClearanceDoor || !Door->ConflictsWithOpening(*NPC,Start,Destination)) continue;
			if (!Door->ConflictsWithOpening(*NPC,Start,Start)
				|| Door->ConflictsWithOpening(*NPC,Destination,Destination)) return false;
		}
		if (!NPC->GetNavigator()) return false;
		FVector Supported;
		if (!IsPhysicalCorridorClearFrom(StartFeet,EndFeet,false,&Supported)) return false;
		if (ClearanceDoor)
		{
			if (FMath::Abs(Supported.Z-StartFeet.Z)>Movement->MaxStepHeight
				|| !NPC->GetNavigator()->IsDoorClearanceSafe(StartFeet,Supported,Frame)) return false;
			ProjectedFeet=Supported;
			return true;
		}
		if (!NPC->GetNavigator()->IsLocalRecoverySafe(StartFeet,EndFeet,Frame)) return false;
		ProjectedFeet=EndFeet;
		return true;
	};
	if (NavData && NavSys->ProjectPointToNavigation(CandidateFeet,Projected,FVector(45,45,FMath::Max(45.f,ReachableRise)),NavData)
		&& FVector::DistSquared2D(CandidateFeet,Projected.Location)<=FMath::Square(45.f) && CheckStep(Projected.Location)) return true;
	// NavMesh erodes small standing spaces beside walls. A local courtesy step can use that
	// real floor only if the complete capsule sweep, support, and heat check all pass.
	return ClearanceDoor && CheckStep(CandidateFeet);
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
void UYUFSLocalMovementComponent::BeginRecovery(AYUFSEvacuationNPC& NPC,const FVector& Target)
{
	RecoveryTarget=Target; RecoveryTime=0.f;
	const float Speed=FMath::Max(20.f,FMath::Min(NPC.GetCharacterMovement()->MaxWalkSpeed,bDoorwayRecovery?175.f:160.f)
		*(bDoorwayRecovery?.65f:.45f));
	RecoveryTimeout=FMath::Clamp(FVector::Dist2D(Target,NPC.GetActorLocation())/Speed+1.f,2.f,8.f);
	NPC.ConsumeMovementInputVector(); NPC.GetCharacterMovement()->StopMovementImmediately();
	NPC.GetCharacterMovement()->bUseRVOAvoidance=false;
	SetState(EYUFSLocalMovementState::Recovering);
}

bool UYUFSLocalMovementComponent::TryDoorwayPassage(const AYUFSInteractionDoor& Door,int32 Frame)
{
	auto* NPC=Cast<AYUFSEvacuationNPC>(GetOwner());
	if (!NPC || !NPC->GetNavigator() || IsRecovering() || !Door.IsPassageClear()) return false;
	const FVector Feet=NPC->GetActorLocation()-FVector(0,0,NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	const FVector DoorCenter=Door.GetPortalFloorPoint(0);
	if (FVector::DistSquared2D(Feet,DoorCenter)>FMath::Square(290.0) || FMath::Abs(Feet.Z-DoorCenter.Z)>80.0) return false;
	const double Side=FVector::DotProduct(Feet-DoorCenter,Door.GetActorForwardVector()) < 0.0 ? -1.0 : 1.0;
	auto Clear=[&](const FVector& From,const FVector& To,FVector& Supported)
	{
		return IsPhysicalCorridorClearFrom(From,To,false,&Supported)
			&& NPC->GetNavigator()->IsLocalRecoverySafe(From,Supported,Frame);
	};
	for (const double Width : {.55,.68,.42,.76,.32})
	{
		const FVector Landing=Door.GetPortalFloorPoint(-Side*115.0,Width);
		FVector Supported;
		if (Clear(Feet,Landing,Supported))
		{
			bDoorwayRecovery=true; DoorwayRecoveryDoor=&Door; DoorwayFollowThrough.Reset();
			BeginRecovery(*NPC,Supported);
			return true;
		}
		const FVector Alignment=Door.GetPortalFloorPoint(Side*110.0,Width);
		FVector SupportedAlignment;
		if (FVector::DistSquared2D(Alignment,Feet)<FMath::Square(35.0)
			|| !Clear(Feet,Alignment,SupportedAlignment) || !Clear(SupportedAlignment,Landing,Supported)) continue;
		bDoorwayRecovery=true; DoorwayRecoveryDoor=&Door; DoorwayFollowThrough=Supported;
		BeginRecovery(*NPC,SupportedAlignment);
		return true;
	}
	return false;
}

bool UYUFSLocalMovementComponent::TryAroundOpenLeaf(const AYUFSInteractionDoor& Door,const FVector& Desired,int32 Frame)
{
	auto* NPC=Cast<AYUFSEvacuationNPC>(GetOwner());
	if (!NPC || !NPC->GetNavigator() || !Door.Panel || !Door.IsPassageClear() || IsRecovering()) return false;
	const auto* Cap=NPC->GetCapsuleComponent();
	const FVector Feet=NPC->GetActorLocation()-FVector(0,0,Cap->GetScaledCapsuleHalfHeight());
	const FTransform Leaf=Door.Panel->GetComponentTransform();
	FVector Tip=Leaf.TransformPosition(FVector(0,50,0)); Tip.Z=Feet.Z;
	const FVector Axis=Leaf.GetUnitAxis(EAxis::Y).GetSafeNormal2D();
	if (FVector::DistSquared2D(Feet,Tip)>FMath::Square(320.0) || Axis.IsNearlyZero()) return false;
	const FVector Across(-Axis.Y,Axis.X,0);
	const double Side=FVector::DotProduct(Feet-Tip,Across)<0.0 ? -1.0 : 1.0;
	const double Margin=Cap->GetScaledCapsuleRadius()+18.0;
	for (const double Extra : {0.0,25.0,55.0})
	{
		const FVector Corner=Tip+Axis*(Margin+Extra)+Across*Side*Margin;
		const FVector Through=Tip+Axis*(Margin+Extra)-Across*Side*(Margin+40.0);
		if (FVector::DotProduct(Through-Feet,Desired)<25.0) continue;
		FVector SafeCorner,SafeThrough;
		if (!IsPhysicalCorridorClearFrom(Feet,Corner,false,&SafeCorner)
			|| !IsPhysicalCorridorClearFrom(SafeCorner,Through,false,&SafeThrough)
			|| !NPC->GetNavigator()->IsLocalRecoverySafe(Feet,SafeCorner,Frame)
			|| !NPC->GetNavigator()->IsLocalRecoverySafe(SafeCorner,SafeThrough,Frame)) continue;
		bDoorwayRecovery=true; DoorwayRecoveryDoor=&Door; DoorwayFollowThrough=SafeThrough;
		BeginRecovery(*NPC,SafeCorner);
		return true;
	}
	return false;
}

bool UYUFSLocalMovementComponent::TryRecovery(FVector Desired,int32 Frame,bool bSeekCleanerAir)
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
		{
			UE_LOG(LogTemp,Log,TEXT("[YUFS][Traffic] agent=%s blockedBy=%s component=%s"),*NPC->GetName(),*GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()));
			if (!bSeekCleanerAir)
				if (const auto* Door=Cast<AYUFSInteractionDoor>(Hit.GetActor()); Door && NPC->GetNavigator())
				{
					const FVector Feet=NPC->GetActorLocation()-FVector(0,0,Capsule->GetScaledCapsuleHalfHeight());
					FVector Crossing;
					const bool Crosses=Door->IntersectPortal(Feet,NPC->GetNavigator()->GetNextWaypoint(),Crossing);
					if ((Crosses && TryDoorwayPassage(*Door,Frame)) || TryAroundOpenLeaf(*Door,Desired,Frame))
					{
						UE_LOG(LogTemp,Log,TEXT("[YUFS][Portal] %s detours around the open leaf of %s"),*NPC->GetName(),*Door->GetName());
						return true;
					}
				}
		}
	}
	const FVector Back=-Desired, Side=FVector(-Desired.Y,Desired.X,0)*(GetTrafficPriorityId(NPC)%2 ? 1.f : -1.f);
	const FVector Feet=NPC->GetActorLocation()-FVector(0,0,NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	const FVector BreathingOffset(0,0,NPC->GetBreathingHeightCm());
	FYUFSHazardSnapshot Snapshot;
	FYUFSHazardSample CurrentAir;
	if (bSeekCleanerAir)
	{
		if (!NPC->GetNavigator()) return false;
		Snapshot = NPC->GetNavigator()->GetPerceivedHazardSnapshot(Frame);
		const FVector BreathingPoint = NPC->GetPawnViewLocation();
		CurrentAir = Snapshot.Sample(BreathingPoint);
		if (CurrentAir.Status != EYUFSHazardDataStatus::Ready || !Snapshot.IsObservedAt(BreathingPoint)) return false;
	}
	TArray<FVector> Candidates;
	for (float Distance:{90.f,150.f,210.f}) for (FVector D:{Back,(Back+Side).GetSafeNormal(),(Back-Side).GetSafeNormal(),Side,-Side,
		(Desired+Side).GetSafeNormal(),(Desired-Side).GetSafeNormal()})
	{
		FVector P;
		if (!IsCandidateReachable(Feet+D*Distance,Frame,P) || FVector::DistSquared2D(P,Feet)<FMath::Square(50.f)) continue;
		if (bSeekCleanerAir)
		{
			const FVector BreathingPoint = P + BreathingOffset;
			const auto Air = Snapshot.Sample(BreathingPoint);
			if (Air.Status != EYUFSHazardDataStatus::Ready || !Snapshot.IsObservedAt(BreathingPoint)) continue;
			if (Air.Smoke > CurrentAir.Smoke + 0.05f || Air.Heat > CurrentAir.Heat + 0.05f
				|| (Air.Smoke > CurrentAir.Smoke - 0.1f && Air.Heat > CurrentAir.Heat - 0.1f)) continue;
		}
		Candidates.Add(P);
	}
	const int32 Selected=SelectRecoverySpace(Candidates,Desired);
	if (Selected!=INDEX_NONE)
	{
		const FVector P=Candidates[Selected];
		// An exhausted batch only resumes after a candidate passes the current space/hazard checks.
		if (Exhausted) Attempts=1;
		BeginRecovery(*NPC,P);
		return true;
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
	const auto* Door=DoorwayRecoveryDoor.Get();
	const FVector Feet=NPC->GetActorLocation()-FVector(0,0,NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	if (Arrived && bDoorwayRecovery && DoorwayFollowThrough.IsSet() && Door && Door->IsPassageClear()
		&& NPC->GetNavigator() && IsPhysicalCorridorClear(DoorwayFollowThrough.GetValue(),false,&Checked)
		&& NPC->GetNavigator()->IsLocalRecoverySafe(Feet,Checked,Frame))
	{
		DoorwayFollowThrough.Reset();
		BeginRecovery(*NPC,Checked);
		return true;
	}
	const bool Clear=Arrived || (bDoorwayRecovery
		? Door && Door->IsPassageClear() && NPC->GetNavigator() && IsPhysicalCorridorClear(RecoveryTarget,false,&Checked)
			&& NPC->GetNavigator()->IsLocalRecoverySafe(Feet,Checked,Frame)
		: IsCandidateReachable(RecoveryTarget,Frame,Checked));
	if (Arrived || RecoveryTime>RecoveryTimeout || !Clear)
	{
		bDoorwayRecovery=false; DoorwayFollowThrough.Reset(); DoorwayRecoveryDoor.Reset();
		NPC->ConsumeMovementInputVector(); NPC->GetCharacterMovement()->StopMovementImmediately();
		NPC->GetCharacterMovement()->bUseRVOAvoidance=true;
		YieldingTo.Reset(); YieldTime=0.f; Cooldown=RecoveryCooldownSeconds;
		SetState(Arrived ? EYUFSLocalMovementState::Following : EYUFSLocalMovementState::Blocked);
		if (NPC->GetNavigator()) NPC->GetNavigator()->ReplanPath(Frame,EYUFSRepathReason::Stuck);
		return true;
	}
	NPC->AddMovementInput((RecoveryTarget-NPC->GetActorLocation()).GetSafeNormal2D(),bDoorwayRecovery?.75f:.45f);
	return true;
}
