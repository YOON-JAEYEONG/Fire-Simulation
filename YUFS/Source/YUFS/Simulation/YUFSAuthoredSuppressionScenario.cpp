#include "Simulation/YUFSAuthoredSuppressionScenario.h"
#include "Simulation/YUFSSimulationController.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "NPC/Integration/YUFSNpcSuppressionComponent.h"
#include "Fire/YUFSFireExtinguisher.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Components/CapsuleComponent.h"

namespace
{
bool FindSprayPosition(AYUFSEvacuationNPC* Npc, AYUFSFireExtinguisher* Tool, FVector Target, FVector& OutFeet)
{
	auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Npc->GetWorld());
	if (!Nav) return false;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(AuthoredSprayPosition), false, Npc);
	Query.AddIgnoredActor(Tool);
	// Other residents move; validate static geometry here and actual sight at execution.
	for (TActorIterator<AYUFSEvacuationNPC> Peer(Npc->GetWorld()); Peer; ++Peer) Query.AddIgnoredActor(*Peer);
	const auto* Capsule = Npc->GetCapsuleComponent();
	float Best = FLT_MAX;
	for (float Radius : {230.f, 330.f}) for (int32 I = 0; I < 16; ++I)
	{
		const FVector Offset = FRotator(0, I * 22.5f, 0).Vector() * Radius;
		FNavLocation Point;
		if (!Nav->ProjectPointToNavigation(Target + Offset - FVector(0,0,80), Point, FVector(60,60,100))
			|| FMath::Abs(Point.Location.Z - Tool->GetActorLocation().Z) > 80.f) continue;
		const FVector Center = Point.Location + FVector(0,0,Capsule->GetScaledCapsuleHalfHeight() + 3.f);
		if (Npc->GetWorld()->OverlapBlockingTestByProfile(Center, FQuat::Identity, Capsule->GetCollisionProfileName(),
			FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(), Capsule->GetScaledCapsuleHalfHeight()-2.f), Query)
			|| Npc->GetWorld()->LineTraceTestByChannel(Center + FVector(0,0,50), Target, ECC_Visibility, Query)) continue;
		auto* Path = UNavigationSystemV1::FindPathToLocationSynchronously(Npc->GetWorld(), Tool->GetActorLocation(), Point.Location, Npc);
		if (!Path || !Path->IsValid() || Path->IsPartial()) continue;
		const float Length = Path->GetPathLength();
		if (Length < Best) { Best = Length; OutFeet = Point.Location; }
	}
	return Best < FLT_MAX;
}
}

AYUFSAuthoredSuppressionScenario::AYUFSAuthoredSuppressionScenario()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = .25f;
}

void AYUFSAuthoredSuppressionScenario::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bEnabled || bStarted || TargetWorldLocation.ContainsNaN()
		|| UGameplayStatics::GetCurrentLevelName(this, true) != MapName) return;
	AYUFSSimulationController* Controller = nullptr;
	for (TActorIterator<AYUFSSimulationController> It(GetWorld()); It; ++It) { Controller = *It; break; }
	if (!Controller || !Controller->IsNPCSimulationEnabled()) return;
	ScanElapsed += DeltaTime;
	if (ScanElapsed < 1.f) return;
	ScanElapsed = 0.f;
	AYUFSEvacuationNPC* Selected = nullptr;
	AYUFSFireExtinguisher* SelectedTool = nullptr;
	FVector SelectedSprayFeet = FVector::ZeroVector;
	float BestDistance = FLT_MAX;
	for (TActorIterator<AYUFSEvacuationNPC> It(GetWorld()); It; ++It)
	{
		auto* Npc = *It;
		auto* Suppression = Npc->GetSuppressionComponent();
		if (!Suppression || !Suppression->CanStartAuthoredGesture()) continue;
		for (TActorIterator<AYUFSFireExtinguisher> Tool(GetWorld()); Tool; ++Tool)
		{
			const FVector Location = Tool->GetActorLocation();
			const float Distance = FVector::DistSquared(Npc->GetActorLocation(), Location);
			if (Tool->GetOwnerActor() || Tool->GetRemainingAgentNormalized() <= 0.f
				|| Distance >= BestDistance || Distance > FMath::Square(600.f)
				|| FMath::Abs(Location.Z - Npc->GetActorLocation().Z) > 160.f
				|| FVector::Dist2D(Location, TargetWorldLocation) > 600.f
				|| FVector::Dist2D(Location, TargetWorldLocation) < 100.f) continue;
			FCollisionQueryParams Query(SCENE_QUERY_STAT(AuthoredSuppressionSight), false, Npc);
			Query.AddIgnoredActor(*Tool);
			for (TActorIterator<AYUFSEvacuationNPC> Peer(GetWorld()); Peer; ++Peer) Query.AddIgnoredActor(*Peer);
			if (GetWorld()->LineTraceTestByChannel(Npc->GetPawnViewLocation(), Location + FVector(0,0,45), ECC_Visibility, Query)) continue;
			auto* Path = UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(), Npc->GetActorLocation(), Location, Npc);
			if (!Path || !Path->IsValid() || Path->IsPartial()) continue;
			FVector SprayFeet;
			if (!FindSprayPosition(Npc, *Tool, TargetWorldLocation, SprayFeet)) continue;
			Selected = Npc; SelectedTool = *Tool; BestDistance = Distance;
			SelectedSprayFeet = SprayFeet;
		}
	}
	if (Selected && Selected->GetSuppressionComponent()->StartAuthoredApproach(SelectedTool, TargetWorldLocation, SelectedSprayFeet, SprayDurationSeconds))
	{
		bStarted = true;
		UE_LOG(LogTemp, Display, TEXT("[AuthoredSuppression] npc=%s target=%s; authored gesture, NOT an FDS ignition or a probability sample. Player camera unchanged."),
			*Selected->GetName(), *TargetWorldLocation.ToCompactString());
	}
	else if (++UnavailableScans == 15)
	{
		UE_LOG(LogTemp, Warning, TEXT("[AuthoredSuppression] No eligible NPC/tool route to authored target %s. Check same-floor props within 600cm, unobstructed sight, NavMesh and current NPC danger. No teleport or forced pickup was used."),
			*TargetWorldLocation.ToCompactString());
	}
}
