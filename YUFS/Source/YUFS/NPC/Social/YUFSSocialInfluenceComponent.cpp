// Fill out your copyright notice in the Description page of Project Settings.


#include "NPC/Social/YUFSSocialInfluenceComponent.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/World.h"
#include "NPC/Perception/YUFSNPCPerceptionComponent.h"

UYUFSSocialInfluenceComponent::UYUFSSocialInfluenceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECollisionChannel::ECC_Pawn));
}

void UYUFSSocialInfluenceComponent::UpdateSocialContext()
{
	NearbyNPCs.Reset();
	OverlappingActors.Reset();
	ActorsToIgnore.Reset();
	EvacuatingCount = 0;
	bPeerWarning = false;
	bHasNPCNeedingHelp = false;

	if (!GetWorld() || !GetOwner()) return;

	ActorsToIgnore.Add(GetOwner());

	UKismetSystemLibrary::SphereOverlapActors(
		this,
		GetOwner()->GetActorLocation(),
		SocialInfluenceRadius,
		ObjectTypes,
		AYUFSEvacuationNPC::StaticClass(),
		ActorsToIgnore,
		OverlappingActors
	);

	for (AActor* Actor : OverlappingActors)
	{
		AYUFSEvacuationNPC* NPC = Cast<AYUFSEvacuationNPC>(Actor);
		if (IsValid(NPC) && !NPC->IsHidden())
		{
			// Do not see crowds or pass warnings through a wall or between storeys.
			const FVector Origin=GetOwner()->GetActorLocation();
			if (FMath::Abs(Origin.Z-NPC->GetActorLocation().Z)>150.f) continue;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(YUFSPeerSight),false,GetOwner()); Params.AddIgnoredActor(NPC);
			if (GetWorld()->LineTraceTestByChannel(Origin,NPC->GetActorLocation(),ECC_Visibility,Params)) continue;
			NearbyNPCs.Add(NPC);
			if (NPC->GetBehaviorStateMachine() && NPC->GetBehaviorStateMachine()->HasRecentDirectEvidence())
			{
				bPeerWarning=true;
				if (auto* OwnerNPC=Cast<AYUFSEvacuationNPC>(GetOwner()))
					if (OwnerNPC->GetNPCPerceptionComponent() && NPC->GetNPCPerceptionComponent())
						OwnerNPC->GetNPCPerceptionComponent()->ReceiveHazardReport(*NPC->GetNPCPerceptionComponent());
			}

			UYUFSBehaviorStateMachine* StateMachine = NPC->GetBehaviorStateMachine();
			if (StateMachine)
			{
				const EYUFSBehaviorState State = StateMachine->GetCurrentState();

				// 대피 중 인원 집계 — Crawling도 대피 시도 중이므로 포함
				if (State == EYUFSBehaviorState::Evacuating || State == EYUFSBehaviorState::Crawling)
				{
					EvacuatingCount++;
				}

				// 실제로 도움이 필요한 NPC 존재 여부 (이타적 행동 진입 조건)
				if (State == EYUFSBehaviorState::Crawling || State == EYUFSBehaviorState::Incapacitated)
				{
					bHasNPCNeedingHelp = true;
				}
			}
		}
	}

	// 주변 NPC 수가 바뀔 때만 재추첨 — 매 프레임 굴리면 Y/N이 깜빡임
	const int32 CurrentCount = NearbyNPCs.Num();
	if (CurrentCount != CachedNearbyNPCCount)
	{
		const float ProbabilityToHelp = FMath::Clamp(
			1.0f - (CurrentCount * 0.1f * BystanderEffectStrength),
			0.1f,
			1.0f);
		bCachedShouldHelpNearbyNPC = FMath::FRand() <= ProbabilityToHelp;
		CachedNearbyNPCCount = CurrentCount;
	}
}

float UYUFSSocialInfluenceComponent::GetNearbyEvacuatingRatio() const
{
	if (NearbyNPCs.Num() == 0) return 0.f;
	return (float)EvacuatingCount / (float)NearbyNPCs.Num();
}

int32 UYUFSSocialInfluenceComponent::GetNearbyNPCCount() const
{
	return NearbyNPCs.Num();
}

// Compatibility name: return an actual nearby agent's exit, never the midpoint of two exits inside a wall.
FVector UYUFSSocialInfluenceComponent::GetAverageEvacuationDestination() const
{
	float Best=FLT_MAX; FVector Target=FVector::ZeroVector;
	for (const auto& Weak:NearbyNPCs)
	{
		const auto* NPC=Cast<AYUFSEvacuationNPC>(Weak.Get());
		if (!IsValid(NPC) || NPC->IsHidden() || !NPC->GetNavigator() || !NPC->GetNavigator()->IsFollowingPath()) continue;
		const float D=FVector::DistSquared(GetOwner()->GetActorLocation(),NPC->GetActorLocation());
		if (D<Best) { Best=D; Target=NPC->GetNavigator()->GetRequestedDestination(); }
	}
	return Target;
}

FVector UYUFSSocialInfluenceComponent::GetNearestNPCNeedingHelpLocation() const
{
	float   BestDistSq = FLT_MAX;
	FVector BestLoc    = FVector::ZeroVector;
	const FVector OwnerLoc = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;

	for (const auto& CharNPC : NearbyNPCs)
	{
		AYUFSEvacuationNPC* NPC = Cast<AYUFSEvacuationNPC>(CharNPC.Get());
		if (!NPC) continue;

		UYUFSBehaviorStateMachine* SM = NPC->GetBehaviorStateMachine();
		if (!SM) continue;

		const EYUFSBehaviorState State = SM->GetCurrentState();
		if (State != EYUFSBehaviorState::Crawling && State != EYUFSBehaviorState::Incapacitated)
			continue;

		const float DistSq = FVector::DistSquared(OwnerLoc, NPC->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			BestLoc    = NPC->GetActorLocation();
		}
	}

	return BestLoc;
}

bool UYUFSSocialInfluenceComponent::ShouldHelpNearbyNPC() const
{
	// 방관자 효과(Bystander Effect): 주변에 사람이 많을수록 도와줄 확률 감소
	// + 실제 도움이 필요한 NPC(Crawling/Incapacitated)가 존재해야 진입 가능
	return bCachedShouldHelpNearbyNPC && bHasNPCNeedingHelp;
}

float UYUFSSocialInfluenceComponent::GetGroupSpeedMultiplier() const
{
	// 논문(Aguirre 등): 그룹의 크기가 클수록 의견 조율 및 보폭 맞춤으로 인해 이동 속도가 지연됨
	float DelayFactor = NearbyNPCs.Num() * SocialDelayPerMember * 0.01f;
	
	// 과도하게 느려지지 않도록 하한선 설정 (최소 기존 속도의 40%)
	return FMath::Clamp(1.0f - DelayFactor, 0.4f, 1.0f);
}
