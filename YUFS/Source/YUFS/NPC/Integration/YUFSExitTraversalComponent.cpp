#include "NPC/Integration/YUFSExitTraversalComponent.h"
#include "Level/YUFSExitPoint.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "NPC/YUFSEvacuationNPC.h"

UYUFSExitTraversalComponent::UYUFSExitTraversalComponent() { PrimaryComponentTick.bCanEverTick = false; }
void UYUFSExitTraversalComponent::DiscardMovementSample() { bHaveSample = false; Crossing.Reset(); }
void UYUFSExitTraversalComponent::Reset()
{
	if (bRestoreLedgeFlags)
		if (auto* Person = Cast<ACharacter>(GetOwner()))
		{
			Person->GetCharacterMovement()->bCanWalkOffLedges = bSavedWalkOffLedges;
			Person->GetCharacterMovement()->bCanWalkOffLedgesWhenCrouching = bSavedCrouchOffLedges;
		}
	bRestoreLedgeFlags = false;
	DiscardMovementSample(); bExited = false; ExitId = NAME_None; ExitTime = 0; ExteriorDestination = FVector::ZeroVector;
}
bool UYUFSExitTraversalComponent::HasExteriorSupport(const FVector& Direction, float LookAheadCm) const
{
	const auto* Person = Cast<ACharacter>(GetOwner());
	if (!Person || !GetWorld() || Direction.ContainsNaN() || !FMath::IsFinite(LookAheadCm)) return false;
	const auto* Capsule = Person->GetCapsuleComponent();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const FVector Ahead = Direction.GetSafeNormal2D();
	const FVector Side(-Ahead.Y, Ahead.X, 0);
	const FVector Feet = Person->GetActorLocation() - FVector(0,0,Capsule->GetScaledCapsuleHalfHeight());
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ExteriorSupport), false, Person);
	for (float Offset : {-Radius, 0.f, Radius})
	{
		const FVector Probe = Feet + Ahead * (FMath::Max(0.f, LookAheadCm) + Radius) + Side * Offset;
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Probe + FVector(0,0,15), Probe - FVector(0,0,35), ECC_Pawn, Params)
			|| Hit.ImpactNormal.Z < .7f) return false;
	}
	return true;
}
void UYUFSExitTraversalComponent::ObserveMovement(const TArray<AYUFSExitPoint*>& Exits, bool bEvacuating, float Dt)
{
	if (bExited) return;
	const auto* Person = Cast<ACharacter>(GetOwner());
	if (!Person || !bEvacuating || Dt <= 0 || !FMath::IsFinite(Dt)) { DiscardMovementSample(); return; }
	const FVector Now = Person->GetActorLocation();
	const FVector Before = PreviousPosition;
	PreviousPosition = Now;
	if (!bHaveSample) { bHaveSample = true; return; }
	// Reject discontinuities; all explicit relocation paths also discard the sample.
	const float MaxTravel = FMath::Max(Person->GetCharacterMovement()->GetMaxSpeed(), Person->GetVelocity().Size()) * Dt + 30.f;
	if (Now.ContainsNaN() || FVector::DistSquared(Now, Before) > FMath::Square(MaxTravel)) { Crossing.Reset(); return; }
	const float Radius = Person->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float HalfHeight = Person->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	if (Crossing.IsValid())
	{
		const FVector Local = Crossing->GetPortalTransform().InverseTransformPosition(Now);
		if (Local.X < 0 || FMath::Abs(Local.Y) + Radius > Crossing->ExitWidth * .5f
			|| !Crossing->ValidatePhysicalTravel(Before, Now, Radius, HalfHeight, Person)) Crossing.Reset();
	}
	if (!Crossing.IsValid())
	{
		for (auto* Exit : Exits)
			if (IsValid(Exit) && Exit->CrossesOutward(Before, Now, Radius, HalfHeight)
				&& Exit->ValidatePhysicalTravel(Before, Now, Radius, HalfHeight, Person)) { Crossing = Exit; break; }
	}
	if (Crossing.IsValid() && Crossing->IsExteriorClear(Now, Radius, HalfHeight))
	{
		bExited = true; ExitId = Crossing->ExitID; ExitTime = GetWorld()->GetTimeSeconds();
		auto* Movement = Person->GetCharacterMovement();
		bSavedWalkOffLedges = Movement->bCanWalkOffLedges;
		bSavedCrouchOffLedges = Movement->bCanWalkOffLedgesWhenCrouching;
		bRestoreLedgeFlags = true;
		Movement->bCanWalkOffLedges = false;
		Movement->bCanWalkOffLedgesWhenCrouching = false;
		const auto* Npc = Cast<AYUFSEvacuationNPC>(Person);
		ExteriorDestination = Crossing->GetExteriorWaitingDestination(Npc ? Npc->GetStableNPCId() : 0);
		UE_LOG(LogTemp, Display, TEXT("[YUFS][Exit] %s physically cleared %s at %.3f"), *Person->GetName(), *ExitId.ToString(), ExitTime);
	}
}
