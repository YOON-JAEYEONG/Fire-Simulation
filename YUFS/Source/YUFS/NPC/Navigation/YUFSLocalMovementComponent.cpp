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
	PassingAround.Reset(); PassCheckedFor.Reset(); PassTime=0.f; bPacingBehind=false; bMakingWay=false;
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
		if (Peer->GetUniqueID() < Leader->GetUniqueID()) Leader = Peer;
		const auto* Traffic = Peer->FindComponentByClass<UYUFSLocalMovementComponent>();
		if (!Traffic || Traffic->State != EYUFSLocalMovementState::Yielding) return false;
		Peer = Traffic->YieldingTo.Get();
	}
	return false;
}
FVector UYUFSLocalMovementComponent::ResolveDirection(FVector Desired,float Dt,int32 Frame)
{
	auto* NPC=Cast<AYUFSEvacuationNPC>(GetOwner()); if (!NPC) return Desired;
	bPacingBehind=false;

	if (FVector::DistSquared2D(LastProgressPosition,NPC->GetActorLocation())>FMath::Square(180.f))
	{ Attempts=0; LastProgressPosition=NPC->GetActorLocation(); }
	AYUFSEvacuationNPC* Blocker=nullptr;
	bool bBlockerMoving=false;
	bool bBlockerAheadOnRoute=false;
	FVector BlockerDir=FVector::ZeroVector;
	const FVector A=NPC->GetActorLocation();
	const float Radius=NPC->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const FVector AFeet=A-FVector(0,0,NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	const auto* MyNav=NPC->GetNavigator();
	const float Now=GetWorld()->GetTimeSeconds();
	// Walking around someone: finished once they are beside/behind us (or it is taking too long).
	if (const AYUFSEvacuationNPC* Passed=PassingAround.Get())
	{
		PassTime+=Dt;
		const FVector B=Passed->GetActorLocation();
		if (Passed->IsHidden() || PassTime>4.f || FMath::Abs(A.Z-B.Z)>15.f
			|| FVector::DotProduct(B-A,Desired.GetSafeNormal2D())<-0.5f*Radius)
			PassingAround.Reset();
	}
	float Nearest=FLT_MAX;
	for (TActorIterator<AYUFSEvacuationNPC> It(GetWorld()); It; ++It)
	{
		auto* Other=*It;
		if (Other==NPC || Other==PassingAround.Get() || Other->IsHidden() || !Other->GetActorEnableCollision()) continue;
		const FVector B=Other->GetActorLocation();
		const float Dist=FVector::DistSquared(A,B);
		if (Dist>FMath::Square(LookAheadCm*2.f) || FMath::Abs(A.Z-B.Z)>120.f) continue;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(YUFSTrafficSight),false,NPC); Params.AddIgnoredActor(Other);
		if (GetWorld()->LineTraceTestByChannel(A,B,ECC_Visibility,Params)) continue;
		const auto* Nav=Other->GetNavigator();
		const bool Moving=IsMovingPeer(Nav && Nav->IsFollowingPath(),Other->IsInteractionHoldingPosition());
		const FVector OtherDir=Moving ? (Nav->GetSteeringTarget(B,120.f)-B).GetSafeNormal2D() : FVector::ZeroVector;
		if (!TrajectoriesConflict(A,Desired,Radius,B,OtherDir,Other->GetCapsuleComponent()->GetScaledCapsuleRadius(),LookAheadCm)) continue;
		// Queue order along the route comes before the ID tie-break, also where a switchback stair
		// turns two people in the same queue to face opposite ways: wait for the one ahead of us on
		// our route, never for the one behind us on theirs.
		bool bAheadOnRoute=false;
		if (Moving)
		{
			const float Tolerance=Radius+Other->GetCapsuleComponent()->GetScaledCapsuleRadius();
			const FVector BFeet=B-FVector(0,0,Other->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
			const FVector OtherHeading=Other->GetVelocity().Size2D()>20.f ? Other->GetVelocity().GetSafeNormal2D() : OtherDir;
			FVector RouteDir;
			bAheadOnRoute=MyNav && MyNav->GetRouteDirectionNear(BFeet,Tolerance,400.f,RouteDir)
				&& FVector::DotProduct(OtherHeading,RouteDir)>0.3f;
			if (!bAheadOnRoute && Nav->GetRouteDirectionNear(AFeet,Tolerance,400.f,RouteDir)
				&& FVector::DotProduct(Desired,RouteDir)>0.3f) continue; // they are behind us: they wait
		}
		// A stationary person is a real obstacle. Moving agents use a stable right of way.
		if ((!Moving || bAheadOnRoute || ShouldYieldTo(A,Desired,NPC->GetUniqueID(),B,OtherDir,Other->GetUniqueID())) && !IsCycleLeader(Other) && Dist<Nearest)
		{ Blocker=Other; Nearest=Dist; bBlockerMoving=Moving; BlockerDir=OtherDir; bBlockerAheadOnRoute=bAheadOnRoute; }
	}
	if (Blocker)
	{
		const FVector B=Blocker->GetActorLocation();
		const float OtherRadius=Blocker->GetCapsuleComponent()->GetScaledCapsuleRadius();
		const float Ahead=FVector::DotProduct(B-A,Desired);
		const bool bSameWay=bBlockerMoving && (FVector::DotProduct(Desired,BlockerDir)>0.5f || bBlockerAheadOnRoute);
		const bool bInFront=Ahead>0.f || bBlockerAheadOnRoute;
		const float BlockerSpeed=Blocker->GetVelocity().Size2D();
		const float MyPace=FMath::Max(1.f,NPC->GetCharacterMovement()->MaxWalkSpeed);
		const auto* BlockerTraffic=Blocker->GetLocalMovement();
		// Nobody waits behind a person who is just standing there (not busy with a door, a bag or a
		// helper), nor behind one walking far slower who is not queueing themselves: go around.
		const bool bStanding=!bBlockerMoving && BlockerSpeed<30.f;
		const bool bSlowWalker=bSameWay && BlockerSpeed<OvertakeSpeedFraction*MyPace
			&& (!BlockerTraffic || !BlockerTraffic->IsDeliberatelyWaiting());
		if (bPassPeopleInTheWay && Ahead>0.f && !Blocker->IsInteractionHoldingPosition() && (bStanding || bSlowWalker)
			&& (PassCheckedFor.Get()!=Blocker || Now>=PassCheckAgainAt))
		{
			PassCheckedFor=Blocker; PassCheckAgainAt=Now+0.25f;
			float Side=1.f, Offset=0.f;
			if (FindPassSide(Blocker,Desired,Frame,Side,Offset))
			{
				PassingAround=Blocker; PassSide=Side; PassOffset=Offset; PassTime=0.f;
				YieldingTo.Reset(); YieldTime=0.f;
				SetState(EYUFSLocalMovementState::Passing);
				return PassSteering(Blocker,Desired);
			}
		}
		// Walking behind someone going the same way: keep their pace instead of stop-and-go.
		if (bSameWay && bInFront)
		{
			const float Gap=FVector::Dist2D(A,B)-Radius-OtherRadius;
			const float Closing=FMath::Clamp((Gap-QueueStopGapCm)/FMath::Max(1.f,QueuePaceGapCm-QueueStopGapCm),0.f,1.25f);
			const float Fraction=FMath::Clamp(BlockerSpeed*Closing/MyPace,0.f,1.f);
			if (Fraction>0.05f)
			{
				YieldingTo.Reset(); YieldTime=0.f; bPacingBehind=true;
				SetState(EYUFSLocalMovementState::Following);
				return Desired*Fraction;
			}
		}
		if (YieldingTo.Get()!=Blocker || FVector::DistSquared2D(LastBlockerPosition,Blocker->GetActorLocation())>FMath::Square(100.f))
		{
			YieldingTo=Blocker; YieldTime=0.f; Attempts=0; LastBlockerPosition=Blocker->GetActorLocation();
		}
		YieldTime+=Dt;
		if (State!=EYUFSLocalMovementState::Blocked || Cooldown<=0.f && Attempts<MaxRecoveryAttempts)
			SetState(EYUFSLocalMovementState::Yielding);
		NPC->ConsumeMovementInputVector(); NPC->GetCharacterMovement()->StopMovementImmediately();
		// Someone standing in a doorway or a narrow spot who is not going anywhere: ask them to step aside.
		if (!bBlockerMoving && YieldTime>MakeWayAfterSeconds && !Blocker->IsInteractionHoldingPosition())
			if (auto* Traffic=Blocker->GetLocalMovement()) Traffic->RequestMakeWay(NPC,Desired,Frame);
		// Yield first. If standing still blocks the winner too, make room by a checked physical back/side step.
		if (Attempts<MaxRecoveryAttempts && YieldTime>FMath::Max(MinYieldSeconds,0.3f) && Cooldown<=0.f &&
			(Nearest<FMath::Square(Radius+Blocker->GetCapsuleComponent()->GetScaledCapsuleRadius()+40.f) || YieldTime>3.f))
			TryRecovery(Desired,Frame,bBlockerMoving && !bSameWay ? Blocker : nullptr);
		return FVector::ZeroVector;
	}
	// The other agent has cleared the conflict; don't keep an expired reservation of the doorway.
	YieldingTo.Reset(); YieldTime=0.f;
	if (const AYUFSEvacuationNPC* Passed=PassingAround.Get())
	{
		SetState(EYUFSLocalMovementState::Passing);
		return PassSteering(Passed,Desired);
	}
	SetState(EYUFSLocalMovementState::Following);
	return Desired;
}
bool UYUFSLocalMovementComponent::FindPassSide(const AYUFSEvacuationNPC* Other,FVector Desired,int32 Frame,float& OutSide,float& OutOffset) const
{
	const auto* NPC=Cast<AYUFSEvacuationNPC>(GetOwner()); if (!NPC || !Other || !GetWorld()) return false;
	const FVector D=Desired.GetSafeNormal2D(); if (D.IsNearlyZero()) return false;
	const FVector Side(-D.Y,D.X,0.f);
	const FVector A=NPC->GetActorLocation(), B=Other->GetActorLocation();
	const float RA=NPC->GetCapsuleComponent()->GetScaledCapsuleRadius(), RB=Other->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float Half=NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float Needed=RA+RB+3.f, Wanted=RA+RB+FMath::Max(3.f,PassClearanceCm);
	// Only on a level floor, with both of us on it: on a stair a sidestep ends in the stairwell.
	if (FMath::Abs(A.Z-B.Z)>15.f) return false;
	{
		FHitResult Ground;
		FCollisionQueryParams GroundParams(SCENE_QUERY_STAT(YUFSPassGround),false,NPC);
		const FVector Feet=A-FVector(0.f,0.f,Half);
		if (!GetWorld()->LineTraceSingleByChannel(Ground,Feet+FVector(0,0,45),Feet-FVector(0,0,60),ECC_Visibility,GroundParams)
			|| Ground.ImpactNormal.Z<0.97f || FMath::Abs(Ground.ImpactPoint.Z-Feet.Z)>10.f) return false;
	}
	// Prefer the side we are already on; a stable tie-break keeps two walkers from mirroring.
	const float Lean=FVector::DotProduct(A-B,Side);
	const float First=FMath::Abs(Lean)>5.f ? FMath::Sign(Lean) : (NPC->GetUniqueID()%2 ? 1.f : -1.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(YUFSPassRoom),false,NPC); Params.AddIgnoredActor(Other);
	for (const float S:{First,-First})
	{
		// Room between the person and the wall beside them: the passer's whole capsule must fit.
		FHitResult Hit;
		const float Room=GetWorld()->LineTraceSingleByChannel(Hit,B,B+Side*S*(Wanted+RA+5.f),ECC_Visibility,Params)
			? Hit.Distance-RA-2.f : Wanted;
		const float Offset=FMath::Min(Wanted,Room);
		if (Offset<Needed) continue;
		const FVector PassFeet=B+Side*S*Offset-FVector(0.f,0.f,Half);
		const FVector BeyondFeet=PassFeet+D*Offset;
		if (!IsStepClear(PassFeet,Frame,Other,10.f)) continue;
		// Walking on past them: level floor all the way and room at the far end.
		if (!HasFloorAlong(PassFeet,BeyondFeet,PassFeet.Z,10.f,Other)
			|| GetWorld()->OverlapBlockingTestByProfile(BeyondFeet+FVector(0,0,Half+2.f),FQuat::Identity,
				NPC->GetCapsuleComponent()->GetCollisionProfileName(),
				FCollisionShape::MakeCapsule(FMath::Max(5.f,RA-1.f),FMath::Max(5.f,Half-3.f)),Params)) continue;
		OutSide=S; OutOffset=Offset;
		return true;
	}
	return false;
}
FVector UYUFSLocalMovementComponent::PassSteering(const AYUFSEvacuationNPC* Other,FVector Desired) const
{
	const auto* NPC=Cast<AYUFSEvacuationNPC>(GetOwner());
	const FVector D=Desired.GetSafeNormal2D();
	if (!NPC || !Other || D.IsNearlyZero()) return Desired;
	const FVector Side(-D.Y,D.X,0.f);
	const FVector A=NPC->GetActorLocation(), B=Other->GetActorLocation();
	const float Along=FVector::DotProduct(A-B,D); // negative while still behind them
	// Swing out beside them first, then walk on past, keeping to the chosen side.
	const FVector Aim=B+Side*PassSide*PassOffset+D*(Along<-0.3f*PassOffset ? -0.3f*PassOffset : PassOffset);
	FVector Dir=Aim-A; Dir.Z=0.f;
	return Dir.IsNearlyZero(1.f) ? D : Dir.GetSafeNormal();
}
bool UYUFSLocalMovementComponent::HasFloorAlong(FVector FromFeet,FVector ToFeet,float FeetZ,float MaxRise,const AActor* Ignore) const
{
	const auto* NPC=Cast<AYUFSEvacuationNPC>(GetOwner()); if (!NPC || !GetWorld()) return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(YUFSStepFloor),false,NPC);
	if (Ignore) Params.AddIgnoredActor(Ignore);
	const int32 Count=FMath::Max(1,FMath::CeilToInt(FVector::Dist2D(FromFeet,ToFeet)/25.f));
	for (int32 I=0; I<=Count; ++I)
	{
		FVector P=FMath::Lerp(FromFeet,ToFeet,float(I)/Count); P.Z=FeetZ;
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit,P+FVector(0,0,45),P-FVector(0,0,60),ECC_Visibility,Params)
			|| Hit.ImpactNormal.Z<0.65f || FMath::Abs(Hit.ImpactPoint.Z-FeetZ)>MaxRise) return false;
	}
	return true;
}
bool UYUFSLocalMovementComponent::IsStepClear(FVector TargetFeet,int32 Frame,const AActor* Ignore,float MaxRise) const
{
	const auto* NPC=Cast<AYUFSEvacuationNPC>(GetOwner()); if (!NPC || !GetWorld() || TargetFeet.ContainsNaN()) return false;
	const auto* Cap=NPC->GetCapsuleComponent(); const float Half=Cap->GetScaledCapsuleHalfHeight();
	const float Rise=MaxRise>=0.f ? MaxRise : NPC->GetCharacterMovement()->MaxStepHeight;
	const FVector StartFeet=NPC->GetActorLocation()-FVector(0,0,Half);
	if (FMath::Abs(TargetFeet.Z-StartFeet.Z)>Rise) return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(YUFSStepClear),false,NPC);
	if (Ignore) Params.AddIgnoredActor(Ignore);
	const FCollisionShape Shape=FCollisionShape::MakeCapsule(FMath::Max(5.f,Cap->GetScaledCapsuleRadius()-1.f),FMath::Max(5.f,Half-3.f));
	const FVector End=TargetFeet+FVector(0,0,Half+2.f);
	// Nothing in between (a wall, a door jamb, a desk, another person) and the spot itself is free.
	FHitResult Hit;
	if (GetWorld()->SweepSingleByProfile(Hit,NPC->GetActorLocation()+FVector(0,0,2),End,FQuat::Identity,Cap->GetCollisionProfileName(),Shape,Params)) return false;
	if (GetWorld()->OverlapBlockingTestByProfile(End,FQuat::Identity,Cap->GetCollisionProfileName(),Shape,Params)) return false;
	// Floor under the whole step at this storey: never over a stair edge or into a stairwell.
	if (!HasFloorAlong(StartFeet,TargetFeet,StartFeet.Z,Rise,Ignore)) return false;
	// Walkable for the navigator, in a world that has navigation.
	if (auto* NavSys=UNavigationSystemV1::GetCurrent(GetWorld()))
		if (const auto* NavData=NavSys->GetNavDataForProps(NPC->GetNavAgentPropertiesRef(),StartFeet))
		{
			FNavLocation Projected;
			if (!NavSys->ProjectPointToNavigation(TargetFeet,Projected,FVector(45,45,45),NavData)) return false;
		}
	// Never into smoke or heat this person knows about (unless already crossing smoke to get out).
	if (const auto* Nav=NPC->GetNavigator())
		if (!Nav->IsEscapingThroughSmoke() && Nav->GetPerceivedHazardSnapshot(Frame).Status==EYUFSHazardDataStatus::Ready
			&& !Nav->IsLocalRecoverySafe(StartFeet,TargetFeet,Frame)) return false;
	return true;
}
bool UYUFSLocalMovementComponent::RequestMakeWay(const AYUFSEvacuationNPC* Requester,FVector RequesterDirection,int32 Frame)
{
	auto* NPC=Cast<AYUFSEvacuationNPC>(GetOwner());
	if (!NPC || !Requester || !GetWorld() || NPC->IsHidden() || IsRecovering() || GetWorld()->GetTimeSeconds()<MakeWayReadyAt) return false;
	// Only a person not walking anywhere steps aside; walkers already follow the right of way.
	if (NPC->IsInteractionHoldingPosition() || (NPC->GetNavigator() && NPC->GetNavigator()->IsFollowingPath())) return false;
	const FVector D=RequesterDirection.GetSafeNormal2D(); if (D.IsNearlyZero()) return false;
	MakeWayReadyAt=GetWorld()->GetTimeSeconds()+1.f;
	const FVector Side(-D.Y,D.X,0.f);
	const float Lean=FVector::DotProduct(NPC->GetActorLocation()-Requester->GetActorLocation(),Side);
	const float S=FMath::Abs(Lean)>5.f ? FMath::Sign(Lean) : (NPC->GetUniqueID()%2 ? 1.f : -1.f);
	const FVector Feet=NPC->GetActorLocation()-FVector(0,0,NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	// Off the requester's line: to the side already leaned toward, the other side, or on ahead of them.
	for (const float Distance:{95.f,130.f,165.f})
		for (const FVector Dir:{Side*S,-Side*S,(Side*S+D).GetSafeNormal(),(D-Side*S).GetSafeNormal(),D})
		{
			const FVector Target=Feet+Dir*Distance;
			if (!IsStepClear(Target,Frame,nullptr)) continue;
			RecoveryTarget=Target; RecoveryTime=0.f; bMakingWay=true;
			NPC->ConsumeMovementInputVector();
			NPC->GetCharacterMovement()->bUseRVOAvoidance=false;
			UE_LOG(LogTemp,Log,TEXT("[YUFS][Traffic] agent=%s steps aside %.0f cm for %s"),*GetNameSafe(NPC),Distance,*GetNameSafe(Requester));
			SetState(EYUFSLocalMovementState::Recovering);
			return true;
		}
	return false;
}
bool UYUFSLocalMovementComponent::IsCandidateReachable(FVector CandidateFeet,int32 Frame,FVector& ProjectedFeet) const
{
	const auto* NPC=Cast<AYUFSEvacuationNPC>(GetOwner()); if (!NPC || !GetWorld()) return false;
	const auto* Cap=NPC->GetCapsuleComponent(); const float Half=Cap->GetScaledCapsuleHalfHeight();
	const FVector StartFeet=NPC->GetActorLocation()-FVector(0,0,Half);
	auto* NavSys=UNavigationSystemV1::GetCurrent(GetWorld()); if (!NavSys) return false;
	const auto* NavData=NavSys->GetNavDataForProps(NPC->GetNavAgentPropertiesRef(),StartFeet); if (!NavData) return false;
	FNavLocation Projected;
	if (!NavSys->ProjectPointToNavigation(CandidateFeet,Projected,FVector(45,45,45),NavData)) return false;
	ProjectedFeet=Projected.Location;
	if (FMath::Abs(ProjectedFeet.Z-StartFeet.Z)>NPC->GetCharacterMovement()->MaxStepHeight ||
		FVector::DistSquared2D(CandidateFeet,ProjectedFeet)>FMath::Square(45.f)) return false;
	return IsPhysicalCorridorClear(ProjectedFeet) && NPC->GetNavigator() &&
		NPC->GetNavigator()->IsLocalRecoverySafe(StartFeet,ProjectedFeet,Frame);
}
bool UYUFSLocalMovementComponent::IsPhysicalCorridorClear(FVector TargetFeet) const
{
	const auto* NPC=Cast<AYUFSEvacuationNPC>(GetOwner()); if (!NPC || !GetWorld()) return false;
	const auto* Cap=NPC->GetCapsuleComponent(); const float Half=Cap->GetScaledCapsuleHalfHeight();
	const FVector StartFeet=NPC->GetActorLocation()-FVector(0,0,Half);
	if (TargetFeet.ContainsNaN() || FMath::Abs(TargetFeet.Z-StartFeet.Z)>NPC->GetCharacterMovement()->MaxStepHeight) return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(YUFSRecoverySweep),false,NPC);
	const FCollisionShape Shape=FCollisionShape::MakeCapsule(FMath::Max(5.f,Cap->GetScaledCapsuleRadius()-1.f),FMath::Max(5.f,Half-3.f));
	const FVector End=TargetFeet+FVector(0,0,Half+2.f);
	FHitResult Hit;
	if (GetWorld()->SweepSingleByProfile(Hit,NPC->GetActorLocation()+FVector(0,0,2),End,FQuat::Identity,Cap->GetCollisionProfileName(),Shape,Params)) return false;
	if (GetWorld()->OverlapBlockingTestByProfile(End,FQuat::Identity,Cap->GetCollisionProfileName(),Shape,Params)) return false;
	// Continuous floor support prevents stepping into a stairwell void or onto another storey.
	const int32 Count=FMath::Max(1,FMath::CeilToInt(FVector::Dist2D(StartFeet,TargetFeet)/30.f));
	for (int32 I=1; I<=Count; ++I)
	{
		const FVector P=FMath::Lerp(StartFeet,TargetFeet,float(I)/Count);
		if (!GetWorld()->LineTraceSingleByChannel(Hit,P+FVector(0,0,45),P-FVector(0,0,55),ECC_Visibility,Params) || Hit.ImpactNormal.Z<0.65f) return false;
	}
	return true;
}
bool UYUFSLocalMovementComponent::TryRecovery(FVector Desired,int32 Frame,const AYUFSEvacuationNPC* GiveWayTo)
{
	auto* NPC=Cast<AYUFSEvacuationNPC>(GetOwner()); if (!NPC || IsRecovering() || Cooldown>0.f) return false;
	if (Attempts>=FMath::Max(1,MaxRecoveryAttempts)) { SetState(EYUFSLocalMovementState::Blocked); return false; }
	++Attempts; Cooldown=FMath::Max(0.2f,RecoveryCooldownSeconds);
	Desired=Desired.GetSafeNormal2D(); if (Desired.IsNearlyZero()) Desired=NPC->GetActorForwardVector();
	FVector Side=FVector(-Desired.Y,Desired.X,0)*(NPC->GetUniqueID()%2 ? 1.f : -1.f);
	const FVector Back=-Desired;
	// Giving way to someone coming the other way: get off their line (further to the side we are
	// already on) before backing up along it, so they can walk past instead of pushing us back.
	TArray<FVector, TInlineAllocator<5>> Directions{Back,(Back+Side).GetSafeNormal(),(Back-Side).GetSafeNormal(),Side,-Side};
	if (GiveWayTo)
	{
		const float Lean=FVector::DotProduct(NPC->GetActorLocation()-GiveWayTo->GetActorLocation(),Side);
		if (Lean<-5.f) Side=-Side;
		Directions={Side,(Back+Side).GetSafeNormal(),-Side,(Back-Side).GetSafeNormal(),Back};
	}
	const FVector Feet=NPC->GetActorLocation()-FVector(0,0,NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	for (float Distance:{90.f,150.f,210.f}) for (const FVector& D:Directions)
	{
		FVector P;
		if (!IsCandidateReachable(Feet+D*Distance,Frame,P) || FVector::DistSquared2D(P,Feet)<FMath::Square(50.f)) continue;
		RecoveryTarget=P; RecoveryTime=0.f;
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
	if (Arrived || RecoveryTime>2.f || !IsCandidateReachable(RecoveryTarget,Frame,Checked))
	{
		NPC->ConsumeMovementInputVector(); NPC->GetCharacterMovement()->StopMovementImmediately();
		NPC->GetCharacterMovement()->bUseRVOAvoidance=true;
		YieldingTo.Reset(); YieldTime=0.f; Cooldown=RecoveryCooldownSeconds;
		if (bMakingWay)
		{
			// Stepped aside for someone: nothing to re-plan, this person was not walking anywhere.
			bMakingWay=false;
			SetState(EYUFSLocalMovementState::Following);
			return true;
		}
		SetState(Arrived ? EYUFSLocalMovementState::Following : EYUFSLocalMovementState::Blocked);
		if (NPC->GetNavigator()) NPC->GetNavigator()->ReplanPath(Frame,EYUFSRepathReason::Stuck);
		return true;
	}
	NPC->AddMovementInput((RecoveryTarget-NPC->GetActorLocation()).GetSafeNormal2D(),bMakingWay ? 1.f : 0.45f);
	return true;
}
