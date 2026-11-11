#include "NPC/Navigation/YUFSExitKnowledgeComponent.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "NPC/Perception/YUFSNPCPerceptionComponent.h"
#include "Level/YUFSLevelDataManager.h"
#include "Level/YUFSExitPoint.h"
#include "Simulation/YUFSSimulationController.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"

UYUFSExitKnowledgeComponent::UYUFSExitKnowledgeComponent() { PrimaryComponentTick.bCanEverTick=false; }
AYUFSEvacuationNPC* UYUFSExitKnowledgeComponent::OwnerNPC() const { return Cast<AYUFSEvacuationNPC>(GetOwner()); }
AYUFSLevelDataManager* UYUFSExitKnowledgeComponent::Manager() const
{
	if (auto* NPC=OwnerNPC()) if (IsValid(NPC->GetLevelDataManager())) return NPC->GetLevelDataManager();
	if (GetWorld()) for (TActorIterator<AYUFSLevelDataManager> It(GetWorld());It;++It) return *It;
	return nullptr;
}
void UYUFSExitKnowledgeComponent::ResetMemory()
{
	Exits.Reset();Visited.Reset();bInitialized=false;StopSearching();
	Clock=ObservationTimer=NextSearchAt=SearchDeadline=0.f;SearchQueryCount=0;
}
void UYUFSExitKnowledgeComponent::Remember(AYUFSExitPoint* Exit,FName Source,bool bFamiliar)
{
	if (!IsValid(Exit) || Exit->GetWorld()!=GetWorld()) return;
	for (auto& Entry:Exits) if (Entry.Actor.Get()==Exit)
	{
		Entry.Location=Exit->GetActorLocation();Entry.Source=Source;Entry.bFamiliar|=bFamiliar;return;
	}
	if (Exits.Num()<64) Exits.Add({Exit,Exit->GetActorLocation(),Source,bFamiliar});
}
void UYUFSExitKnowledgeComponent::InitializeMemory()
{
	auto* NPC=OwnerNPC();auto* Map=Manager();
	if (bInitialized || !NPC || !Map || !Map->HasActorBegunPlay()) return;
	bInitialized=true;
	SearchRandom.Initialize(HashCombine(GetTypeHash(NPC->ScenarioSeed),GetTypeHash(NPC->GetStableNPCId())));
	Visit(NPC->GetActorLocation());
	AYUFSExitPoint* Entry=nullptr;float Distance=FLT_MAX;
	for (auto* Exit:Map->GetExitActors())
	{
		if (!IsValid(Exit)) continue;
		if (InitiallyKnownExitIDs.Contains(Exit->ExitID) && !Exit->ExitID.IsNone()) Remember(Exit,TEXT("Authored"));
		const bool Eligible=!FamiliarEntryExitID.IsNone() ? Exit->ExitID==FamiliarEntryExitID : bRememberMarkedEntry && Exit->bIsFamiliarEntry;
		const float D=FVector::DistSquared(NPC->GetSpawnLocation(),Exit->GetActorLocation());
		if (Eligible && D<Distance) { Distance=D;Entry=Exit; }
	}
	if (Entry) Remember(Entry,TEXT("Entry"),true);
}
TArray<FVector> UYUFSExitKnowledgeComponent::GetKnownExitLocations() const
{
	TArray<FVector> Result;
	for (const auto& Entry:Exits) if (Entry.Actor.IsValid()) Result.Add(Entry.Location);
	return Result;
}
int32 UYUFSExitKnowledgeComponent::GetKnownExitCount() const { return GetKnownExitLocations().Num(); }
bool UYUFSExitKnowledgeComponent::KnowsExitLocation(FVector Location) const
{
	for (const auto& Entry:Exits) if (Entry.Actor.IsValid() && Entry.Location.Equals(Location,10.f)) return true;
	return false;
}
bool UYUFSExitKnowledgeComponent::TryGetFamiliarExit(FVector& Out) const
{
	for (const auto& Entry:Exits) if (Entry.Actor.IsValid() && Entry.bFamiliar) { Out=Entry.Location;return true; }
	return false;
}
bool UYUFSExitKnowledgeComponent::RememberGuidedExit(FVector Location,FName Source)
{
	if (Location.ContainsNaN() || !Manager()) return false;
	AYUFSExitPoint* Best=nullptr;float Distance=FMath::Square(75.f);
	for (auto* Exit:Manager()->GetExitActors()) if (IsValid(Exit))
	{
		const float D=FVector::DistSquared(Location,Exit->GetActorLocation());
		if (D<=Distance) { Distance=D;Best=Exit; }
	}
	if (!Best) return false;
	Remember(Best,Source);StopSearching();return true;
}
void UYUFSExitKnowledgeComponent::ObserveExits(const FYUFSHazardSnapshot& Snapshot,bool bSmokeActive)
{
	InitializeMemory();auto* NPC=OwnerNPC();auto* Map=Manager();
	if (!NPC || !Map || !NPC->GetNPCPerceptionComponent()) return;
	const FYUFSHazardSnapshot SightField=bSmokeActive?Snapshot:FYUFSHazardSnapshot(); // No smoke is present before the fire starts.
	for (auto* Exit:Map->GetExitActors()) if (IsValid(Exit))
	{
		const auto Sight=NPC->GetNPCPerceptionComponent()->EvaluateSightToPoint(SightField,Exit->GetActorLocation()+FVector(0,0,RecognitionHeightCm));
		if (Sight.bReachedTarget && (!bSmokeActive || Sight.bCompleteSmokeData)) Remember(Exit,TEXT("Seen"));
	}
}
void UYUFSExitKnowledgeComponent::UpdateKnowledge(float DeltaTime)
{
	Clock+=FMath::Max(0.f,DeltaTime);InitializeMemory();ObservationTimer-=DeltaTime;
	if (ObservationTimer>0.f) return;
	ObservationTimer=FMath::Max(.2f,ObservationIntervalSeconds);
	auto* Map=Manager();if (!Map) return;
	bool bSmokeActive=false;
	for (TActorIterator<AYUFSSimulationController> It(GetWorld());It;++It) { bSmokeActive=It->IsNPCSimulationEnabled();break; }
	ObserveExits(Map->GetHazardSnapshot(Map->GetCurrentHazardFrame()),bSmokeActive);
}
void UYUFSExitKnowledgeComponent::Visit(FVector Point)
{
	if (!Visited.IsEmpty() && FVector::Dist2D(Visited.Last(),Point)<100.f) return;
	if (Visited.Num()>=32) Visited.RemoveAt(0);
	Visited.Add(Point);
}
void UYUFSExitKnowledgeComponent::StopSearching() { bSearching=bHasSearchTarget=false; }
bool UYUFSExitKnowledgeComponent::TryGetSearchTarget(FVector& Out)
{
	auto* NPC=OwnerNPC();if (!NPC || !NPC->GetNavigator()) return false;
	InitializeMemory();bSearching=true;
	auto* Nav=NPC->GetNavigator();
	if (Nav->IsWaitingForSaferPath()) return false; // Preserve the rejected-route reassessment gate.
	if (bHasSearchTarget)
	{
		const bool Arrived=FVector::Dist2D(NPC->GetActorLocation(),SearchTarget)<80.f
			&& FMath::Abs(NPC->GetActorLocation().Z-NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-SearchTarget.Z)<100.f;
		if (!Arrived && Clock<SearchDeadline) { Out=SearchTarget;return true; }
		Visit(Arrived?NPC->GetActorLocation():SearchTarget);bHasSearchTarget=false;NextSearchAt=Clock+1.2f;
	}
	if (Clock<NextSearchAt) return false;
	NextSearchAt=Clock+2.f;
	auto* System=UNavigationSystemV1::GetCurrent(GetWorld());if (!System) return false;
	const FVector Feet=NPC->GetActorLocation()-FVector(0,0,NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	const float Radius=FMath::Clamp(SearchRadiusCm,150.f,700.f),Phase=SearchRandom.FRandRange(0.f,360.f);
	float Best=-FLT_MAX;FVector Selected=FVector::ZeroVector;
	const auto Field=Nav->GetPerceivedHazardSnapshot(Manager()?Manager()->GetCurrentHazardFrame():0);
	for (int32 I=0;I<8;++I)
	{
		const FVector Probe=Feet+FRotator(0,Phase+I*45.f,0).Vector()*Radius;
		FNavLocation Candidate;
		if (!System->ProjectPointToNavigation(Probe,Candidate,FVector(75,75,120)) || FVector::Dist2D(Feet,Candidate.Location)<140.f) continue;
		++SearchQueryCount;
		auto* Path=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),NPC->GetActorLocation(),Candidate.Location,NPC);
		if (!Path || !Path->IsValid() || Path->IsPartial() || Path->PathPoints.Num()<2 || Path->GetPathLength()>Radius*2.5f) continue;
		const auto Risk=Field.ScorePath(Path->PathPoints,Nav->GetHazardSettings());if (Risk.bUnsafeAhead) continue;
		float Novelty=Radius*3.f;
		for (const auto& Point:Visited) Novelty=FMath::Min(Novelty,float(FVector::Dist(Point,Candidate.Location)));
		const float Score=Novelty-.15f*Path->GetPathLength()-Risk.AddedCost*.1f;
		if (Score>Best) { Best=Score;Selected=Candidate.Location; }
	}
	if (Best==-FLT_MAX) return false;
	SearchTarget=Selected;bHasSearchTarget=true;SearchDeadline=Clock+FMath::Max(3.f,MaxSearchLegSeconds);Out=SearchTarget;return true;
}
void UYUFSExitKnowledgeComponent::TickSearchLook(float DeltaTime)
{
	auto* NPC=OwnerNPC();if (!NPC || !bSearching || bHasSearchTarget || NPC->bUseExternalMotionDriver || NPC->bUseExternalNavigationDriver) return;
	FRotator Look=NPC->GetActorRotation();Look.Yaw+=60.f*DeltaTime;NPC->SetActorRotation(Look);
}
FString UYUFSExitKnowledgeComponent::GetKnowledgeDiagnostics() const
{
	FString Result=FString::Printf(TEXT("knownExits=%d searchingExit=%d searchTarget=%d searchQueries=%d"),GetKnownExitCount(),bSearching,bHasSearchTarget,SearchQueryCount);
	for (const auto& Entry:Exits) if (Entry.Actor.IsValid()) Result+=FString::Printf(TEXT(" [%s:%s%s]"),*Entry.Actor->ExitID.ToString(),*Entry.Source.ToString(),Entry.bFamiliar?TEXT("/Entry"):TEXT(""));
	return Result;
}
