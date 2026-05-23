// Fill out your copyright notice in the Description page of Project Settings.


#include "NPC/Social/YUFSSocialInfluenceComponent.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/World.h"

// Sets default values for this component's properties
UYUFSSocialInfluenceComponent::UYUFSSocialInfluenceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

// Called when the game starts
void UYUFSSocialInfluenceComponent::BeginPlay()
{
	Super::BeginPlay();
}

// Called every frame
void UYUFSSocialInfluenceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UYUFSSocialInfluenceComponent::UpdateSocialContext()
{
	NearbyNPCs.Empty();
	EvacuatingCount = 0;
	bHasNPCNeedingHelp = false;

	if (!GetWorld() || !GetOwner()) return;

	TArray<AActor*> OverlappingActors;
	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECollisionChannel::ECC_Pawn));

	TArray<AActor*> ActorsToIgnore;
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
		if (NPC)
		{
			NearbyNPCs.Add(NPC);

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

FVector UYUFSSocialInfluenceComponent::GetAverageEvacuationDestination() const
{
	FVector AvgDest = FVector::ZeroVector;
	int32 Count = 0;

	for (ACharacter* CharNPC : NearbyNPCs)
	{
		AYUFSEvacuationNPC* NPC = Cast<AYUFSEvacuationNPC>(CharNPC);
		if (NPC && NPC->GetBehaviorStateMachine() && NPC->GetNavigator())
		{
			if (NPC->GetBehaviorStateMachine()->GetCurrentState() == EYUFSBehaviorState::Evacuating)
			{
				FVector Dest = NPC->GetNavigator()->GetCurrentDestination();
				if (Dest != FVector::ZeroVector)
				{
					AvgDest += Dest;
					Count++;
				}
			}
		}
	}

	if (Count > 0)
	{
		return AvgDest / Count;
	}
	
	return FVector::ZeroVector;
}

FVector UYUFSSocialInfluenceComponent::GetNearestNPCNeedingHelpLocation() const
{
	float   BestDistSq = FLT_MAX;
	FVector BestLoc    = FVector::ZeroVector;
	const FVector OwnerLoc = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;

	for (ACharacter* CharNPC : NearbyNPCs)
	{
		AYUFSEvacuationNPC* NPC = Cast<AYUFSEvacuationNPC>(CharNPC);
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
