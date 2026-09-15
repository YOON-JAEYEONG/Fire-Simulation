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
			if (GetWorld()->LineTraceTestByChannel(Npc->GetPawnViewLocation(), Location + FVector(0,0,45), ECC_Visibility, Query)
				|| GetWorld()->LineTraceTestByChannel(Location + FVector(0,0,90), TargetWorldLocation, ECC_Visibility, Query)) continue;
			auto* Path = UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(), Npc->GetActorLocation(), Location, Npc);
			if (!Path || !Path->IsValid() || Path->IsPartial()) continue;
			Selected = Npc; SelectedTool = *Tool; BestDistance = Distance;
		}
	}
	if (Selected && Selected->GetSuppressionComponent()->StartVisualPresentation(SelectedTool, TargetWorldLocation, SprayDurationSeconds))
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
