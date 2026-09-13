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
FVector UYUFSLocalMovementComponent::ResolveDirection(FVector Desired,float Dt,int32 Frame)
{
	auto* NPC=Cast<AYUFSEvacuationNPC>(GetOwner()); if (!NPC) return Desired;

	if (FVector::DistSquared2D(LastProgressPosition,NPC->GetActorLocation())>FMath::Square(180.f))
	{ Attempts=0; LastProgressPosition=NPC->GetActorLocation(); }
	AYUFSEvacuationNPC* Blocker=nullptr;
	const FVector A=NPC->GetActorLocation();
	const float Radius=NPC->GetCapsuleComponent()->GetScaledCapsuleRadius();
	float Nearest=FLT_MAX;
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
		const bool Moving=Nav && Nav->IsFollowingPath();
		const FVector OtherDir=Moving ? (Nav->GetSteeringTarget(B,120.f)-B).GetSafeNormal2D() : FVector::ZeroVector;
		if (!TrajectoriesConflict(A,Desired,Radius,B,OtherDir,Other->GetCapsuleComponent()->GetScaledCapsuleRadius(),LookAheadCm)) continue;
		// A stationary person is a real obstacle. Moving agents use a stable right of way.
		if ((!Moving || ShouldYieldTo(A,Desired,NPC->GetUniqueID(),B,OtherDir,Other->GetUniqueID())) && Dist<Nearest)
		{ Blocker=Other; Nearest=Dist; }
	}
	if (Blocker)
	{
		if (YieldingTo.Get()!=Blocker || FVector::DistSquared2D(LastBlockerPosition,Blocker->GetActorLocation())>FMath::Square(100.f))
		{
			YieldingTo=Blocker; YieldTime=0.f; Attempts=0; LastBlockerPosition=Blocker->GetActorLocation();
		}
		YieldTime+=Dt;
		if (State!=EYUFSLocalMovementState::Blocked || Cooldown<=0.f && Attempts<MaxRecoveryAttempts)
			SetState(EYUFSLocalMovementState::Yielding);
		NPC->ConsumeMovementInputVector(); NPC->GetCharacterMovement()->StopMovementImmediately();
		// Yield first. If standing still blocks the winner too, make room by a checked physical back/side step.
		if (Attempts<MaxRecoveryAttempts && YieldTime>FMath::Max(MinYieldSeconds,0.3f) && Cooldown<=0.f &&
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
bool UYUFSLocalMovementComponent::TryRecovery(FVector Desired,int32 Frame)
{
	auto* NPC=Cast<AYUFSEvacuationNPC>(GetOwner()); if (!NPC || IsRecovering() || Cooldown>0.f) return false;
	if (Attempts>=FMath::Max(1,MaxRecoveryAttempts)) { SetState(EYUFSLocalMovementState::Blocked); return false; }
	++Attempts; Cooldown=FMath::Max(0.2f,RecoveryCooldownSeconds);
	Desired=Desired.GetSafeNormal2D(); if (Desired.IsNearlyZero()) Desired=NPC->GetActorForwardVector();
	const FVector Back=-Desired, Side=FVector(-Desired.Y,Desired.X,0)*(NPC->GetUniqueID()%2 ? 1.f : -1.f);
	const FVector Feet=NPC->GetActorLocation()-FVector(0,0,NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	for (float Distance:{90.f,150.f,210.f}) for (FVector D:{Back,(Back+Side).GetSafeNormal(),(Back-Side).GetSafeNormal(),Side,-Side})
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
		SetState(Arrived ? EYUFSLocalMovementState::Following : EYUFSLocalMovementState::Blocked);
		if (NPC->GetNavigator()) NPC->GetNavigator()->ReplanPath(Frame,EYUFSRepathReason::Stuck);
		return true;
	}
	NPC->AddMovementInput((RecoveryTarget-NPC->GetActorLocation()).GetSafeNormal2D(),0.45f);
	return true;
}
