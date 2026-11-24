#include "NPC/Navigation/YUFSExitKnowledgeComponent.h"
#include "Algo/Count.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "NPC/Perception/YUFSNPCPerceptionComponent.h"
#include "Level/YUFSLevelDataManager.h"
#include "Level/YUFSExitPoint.h"
#include "Fire/YUFSInteractionDoor.h"
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
	Exits.Reset();Visited.Reset();KnownDoors.Reset();RecentlyRejected.Reset();LastFeet.Reset();bInitialized=false;StopSearching();
	LastExploredDoor.Reset(); LastDoorRetryAt=0.f;
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
		if (KnowsExitLocation(Exit->GetActorLocation())) continue;
		auto CanRead=[&](const FVector& Point)
		{
			const auto Sight=NPC->GetNPCPerceptionComponent()->EvaluateSightToPoint(SightField,Point);
			return Sight.bReachedTarget && (!bSmokeActive || Sight.bCompleteSmokeData);
		};
		if (CanRead(Exit->GetActorLocation()+FVector(0,0,RecognitionHeightCm)))
		{
			Remember(Exit,TEXT("Seen"));
			continue;
		}
		for (const AActor* Marker : Exit->WayfindingMarkers)
		{
			if (!IsValid(Marker) || Marker->IsHidden() || Marker->GetWorld()!=GetWorld()) continue;
			const FVector Label=Marker->GetActorLocation();
			const FVector ToPerson=NPC->GetActorLocation()-Label;
			if (FMath::Abs(ToPerson.Z)>155.f || FVector::DotProduct(ToPerson,Marker->GetActorForwardVector())<=0.0) continue;
			if (CanRead(Label)) { Remember(Exit,TEXT("StairSign"));break; }
		}
	}
}
void UYUFSExitKnowledgeComponent::UpdateKnowledge(float DeltaTime)
{
	Clock+=FMath::Max(0.f,DeltaTime);InitializeMemory();ObservationTimer-=DeltaTime;
	if (ObservationTimer>0.f) return;
	ObservationTimer=FMath::Max(.2f,ObservationIntervalSeconds);
	const auto* NPC=OwnerNPC();
	if (!NPC) return;
	bool bSmokeActive=false;
	for (TActorIterator<AYUFSSimulationController> It(GetWorld());It;++It) { bSmokeActive=It->IsNPCSimulationEnabled();break; }
	auto* Map=Manager();
	const auto Snapshot=Map && bSmokeActive ? Map->GetHazardSnapshot(Map->GetCurrentHazardFrame()) : FYUFSHazardSnapshot();
	ObserveExits(Snapshot,bSmokeActive);
	ObserveLocalDoors(Snapshot,bSmokeActive);
	const FVector Feet=NPC->GetActorLocation()-FVector(0,0,NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	if (LastFeet.IsSet() && FVector::Dist(LastFeet.GetValue(),Feet)<450.f)
	{
		FVector Crossing;
		for (TActorIterator<AYUFSInteractionDoor> It(GetWorld());It;++It)
			if (It->IsOpen() && It->IntersectPortal(LastFeet.GetValue(),Feet,Crossing)) RememberLocalDoor(*It);
	}
	LastFeet=Feet;
	Visit(Feet);
}
void UYUFSExitKnowledgeComponent::RememberLocalDoor(AYUFSInteractionDoor* Door)
{
	if (!IsValid(Door) || Door->GetWorld()!=GetWorld()) return;
	KnownDoors.RemoveAll([](const auto& Entry) { return !Entry.IsValid(); });
	if (KnownDoors.Contains(TWeakObjectPtr<AYUFSInteractionDoor>(Door))) return;
	if (KnownDoors.Num()>=32) KnownDoors.RemoveAt(0);
	KnownDoors.Add(Door);
}
int32 UYUFSExitKnowledgeComponent::GetKnownDoorCount() const
{
	return Algo::CountIf(KnownDoors, [](const auto& Door) { return Door.IsValid(); });
}
void UYUFSExitKnowledgeComponent::ObserveLocalDoors(const FYUFSHazardSnapshot& Snapshot,bool bSmokeActive)
{
	AYUFSEvacuationNPC* NPC=OwnerNPC();
	if (!NPC || !NPC->GetNPCPerceptionComponent()) return;
	const FVector Feet=NPC->GetActorLocation()-FVector(0,0,NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	const auto SightField=bSmokeActive ? Snapshot : FYUFSHazardSnapshot();
	for (TActorIterator<AYUFSInteractionDoor> It(GetWorld());It;++It)
	{
		const auto& Door=**It;
		if (FVector::DistSquared2D(Feet,Door.GetActorLocation())>FMath::Square(650.f)
			|| FMath::Abs(Feet.Z-Door.GetActorLocation().Z)>100.f) continue;
		const FVector Center=Door.GetPortalFloorPoint(0);
		const FVector Toward=Door.GetActorForwardVector();
		const double Side=Door.GetActorTransform().InverseTransformPosition(Feet).X<0.0 ? -1.0 : 1.0;
		const double LeafOffset=FVector::DotProduct(Door.GetHandleLocation()-Center,Toward);
		const FVector Face=Center+Toward*Side*(FMath::Max(0.0,LeafOffset*Side)+18.0)+FVector(0,0,105);
		if (NPC->GetNPCPerceptionComponent()->EvaluateSightToPoint(SightField,Face).bReachedTarget) RememberLocalDoor(*It);
	}
}
AYUFSInteractionDoor* UYUFSExitKnowledgeComponent::FindKnownDoorAlongPath(const TArray<FVector>& Points,int32 NextWaypoint) const
{
	auto* NPC=OwnerNPC();
	if (!NPC || !Points.IsValidIndex(NextWaypoint)) return nullptr;
	FVector From=NPC->GetActorLocation()-FVector(0,0,NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	double Remaining=300.0;
	for (int32 I=NextWaypoint; Points.IsValidIndex(I) && Remaining>0.0; ++I)
	{
		const double Length=FVector::Dist(From,Points[I]);
		if (Length<1.0) continue;
		const FVector To=FMath::Lerp(From,Points[I],FMath::Min(Remaining/Length,1.0));
		AYUFSInteractionDoor* Best=nullptr;
		double Distance=DBL_MAX;
		for (const auto& Entry:KnownDoors)
		{
			auto* Door=Entry.Get();
			FVector Crossing;
			if (!Door || Door->IsPassageClear() || !Door->IsUserInReach(NPC)
				|| !Door->IntersectPortal(From,To,Crossing)) continue;
			const double D=FVector::DistSquared(From,Crossing);
			if (D<Distance) { Distance=D;Best=Door; }
		}
		if (Best) return Best;
		Remaining-=Length;
		From=Points[I];
	}
	return nullptr;
}
void UYUFSExitKnowledgeComponent::Visit(FVector Point)
{
	if (!Visited.IsEmpty() && FVector::Dist(Visited.Last(),Point)<100.f) return;
	if (Visited.Num()>=32) Visited.RemoveAt(0);
	Visited.Add(Point);
}
void UYUFSExitKnowledgeComponent::StopSearching() { bSearching=bHasSearchTarget=false; SearchDoor.Reset(); }
AYUFSInteractionDoor* UYUFSExitKnowledgeComponent::GetPlannedSearchDoor() const
{
	auto* Door=bSearching ? SearchDoor.Get() : nullptr;
	const auto* NPC=OwnerNPC();
	if (!Door || !NPC || Clock>=SearchDeadline) return nullptr;
	const double SignedSide=FVector::DotProduct(NPC->GetActorLocation()-Door->GetPortalFloorPoint(0),Door->GetActorForwardVector());
	return SignedSide*SearchDoorSide>=-65.0 ? Door : nullptr;
}
bool UYUFSExitKnowledgeComponent::TryGetSearchTarget(FVector& Out)
{
	auto* NPC=OwnerNPC();if (!NPC || !NPC->GetNavigator()) return false;
	InitializeMemory();bSearching=true;
	auto* Nav=NPC->GetNavigator();
	const FVector Feet=NPC->GetActorLocation()-FVector(0,0,NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	if (const auto* Planned=SearchDoor.Get())
	{
		const double Side=FVector::DotProduct(Feet-Planned->GetPortalFloorPoint(0),Planned->GetActorForwardVector());
		const bool Crossed=Side*SearchDoorSide < -65.0;
		if (Crossed || Clock>=SearchDeadline || (!Planned->IsPassageClear() && !Planned->CanOperate()))
		{
			LastExploredDoor=SearchDoor; LastDoorRetryAt=Clock+(Crossed?20.f:6.f);
			SearchDoor.Reset(); bHasSearchTarget=false;
		}
		else if (Planned->IsUserInReach(NPC)) return false; // The local door executor now owns the next short leg.
	}
	// A rejected full route is not a refusal to try a different, locally checked leg.
	if (bHasSearchTarget)
	{
		const bool Arrived=FVector::Dist2D(NPC->GetActorLocation(),SearchTarget)<80.f
			&& FMath::Abs(NPC->GetActorLocation().Z-NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-SearchTarget.Z)<100.f;
		const bool Failed=Nav->GetNavigationStatus()==EYUFSNavigationStatus::Failed
			&& Nav->GetRequestedDestination().Equals(SearchTarget,75.f);
		if (!Arrived && !Failed && Clock<SearchDeadline) { Out=SearchTarget;return true; }
		if (!Arrived)
		{
			if (RecentlyRejected.Num()>=10) RecentlyRejected.RemoveAt(0);
			RecentlyRejected.Add({SearchTarget, Clock+12.f});
		}
		if (SearchDoor.IsValid())
		{
			LastExploredDoor=SearchDoor; LastDoorRetryAt=Clock+6.f; SearchDoor.Reset();
		}
		bHasSearchTarget=false;NextSearchAt=Clock+1.2f;
	}
	if (Clock<NextSearchAt) return false;
	NextSearchAt=Clock+2.f;
	RecentlyRejected.RemoveAll([this](const FRejectedPoint& Entry) { return Clock>=Entry.RetryAt; });
	auto* System=UNavigationSystemV1::GetCurrent(GetWorld());if (!System) return false;
	const float Radius=FMath::Clamp(SearchRadiusCm,150.f,700.f),Phase=NPC->GetActorRotation().Yaw;
	float Best=-FLT_MAX;FVector Selected=FVector::ZeroVector;
	AYUFSInteractionDoor* SelectedDoor=nullptr;
	const auto* Map=Manager();
	const int32 Frame=Map ? Map->GetCurrentHazardFrame() : 0;
	const auto Field=Nav->GetPerceivedHazardSnapshot(Frame);
	const auto RawField=Map ? Map->GetHazardSnapshot(Frame) : FYUFSHazardSnapshot();
	const auto Settings=Nav->GetEvacuationHazardSettings();
	auto Consider=[&](const FVector& Probe, AYUFSInteractionDoor* Portal=nullptr, const bool bRememberedRoute=false,
		const float RouteBonus=0.f)
	{
		FNavLocation Candidate;
		if (!System->ProjectPointToNavigation(Probe,Candidate,FVector(55,55,95))
			|| (!Portal && FVector::Dist2D(Feet,Candidate.Location)<105.f)
			|| RecentlyRejected.ContainsByPredicate([&](const FRejectedPoint& Entry) { return FVector::Dist(Entry.Point,Candidate.Location)<100.f; })) return;
		if (Portal && (FVector::DotProduct(Feet-Portal->GetPortalFloorPoint(0),Portal->GetActorForwardVector())
			* FVector::DotProduct(Candidate.Location-Portal->GetPortalFloorPoint(0),Portal->GetActorForwardVector())<=0.0)) return;
		if (!Portal && !bRememberedRoute && !NPC->GetNPCPerceptionComponent()->EvaluateSightToPoint(
			RawField,Candidate.Location+FVector(0,0,95)).bReachedTarget) return;
		++SearchQueryCount;
		const bool InReach=Portal && Portal->IsUserInReach(NPC);
		auto* Path=InReach ? nullptr : UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Feet,Candidate.Location,NPC);
		if (!InReach && (!Path || !Path->IsValid() || Path->IsPartial() || Path->PathPoints.Num()<2 || Path->GetPathLength()>Radius*2.5f)) return;
		const auto Risk=Field.ScorePath(InReach ? TArray<FVector>{Feet,Feet} : Path->PathPoints,Settings);if (Risk.bUnsafeAhead) return;
		float Novelty=Radius*3.f;
		for (const auto& Point:Visited) Novelty=FMath::Min(Novelty,float(FVector::Dist(Point,Candidate.Location)));
		// Do not oscillate between two already visited prefixes of rejected routes.
		if (RouteBonus>0.f && Novelty<90.f) return;
		const float Score=Novelty-.15f*(Path?Path->GetPathLength():0.f)-Risk.AddedCost*.1f+(Portal ? 260.f : 0.f)+RouteBonus;
		if (Score>Best) { Best=Score;Selected=Candidate.Location;SelectedDoor=Portal; }
	};
	// A complete exit route may be unsafe far ahead, without the approach to the
	// next corridor corner or stair landing being unsafe. Check short prefixes of
	// routes to exits this person actually knows before wandering into nearby rooms.
	for (const FVector& Exit : GetKnownExitLocations())
	{
		const auto* Route=UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(),Feet,Exit,NPC);
		if (!Route || !Route->IsValid() || Route->IsPartial() || Route->PathPoints.Num()<2) continue;
		for (const double Distance : {double(Radius),double(Radius)*.67,double(Radius)*.43})
		{
			double Remaining=Distance;
			for (int32 I=1;I<Route->PathPoints.Num();++I)
			{
				const FVector& From=Route->PathPoints[I-1];
				const FVector& To=Route->PathPoints[I];
				const double Length=FVector::Dist(From,To);
				if (Length<1.0) continue;
				if (Remaining<=Length)
				{
					Consider(FMath::Lerp(From,To,Remaining/Length),nullptr,true,Radius*2.f);
					break;
				}
				Remaining-=Length;
			}
		}
	}
	for (const auto& Entry:KnownDoors)
	{
		auto* Door=Entry.Get();
		if (Door && (Door->IsPassageClear() || Door->CanOperate())
			&& (Door!=LastExploredDoor.Get() || Clock>=LastDoorRetryAt)
			&& FVector::Dist(Feet,Door->GetActorLocation())<=Radius+120.f)
		{
			const double Side=FVector::DotProduct(Feet-Door->GetPortalFloorPoint(0),Door->GetActorForwardVector())<0.0 ? -1.0 : 1.0;
			const FVector Beyond=Door->GetPortalFloorPoint(-Side*120.0);
			if (!Nav->IsKnownEvacuationLocationBlocked(Beyond+FVector(0,0,Settings.SampleHeightCm)))
				Consider(Door->GetPortalFloorPoint(Side*110.0),Door);
		}
	}
	for (float Offset : {0.f,-18.f,18.f,-36.f,36.f})
	{
		const FVector Direction=FRotator(0,Phase+Offset,0).Vector();
		Consider(Feet+Direction*Radius);
		Consider(Feet+Direction*Radius*.6f);
	}
	if (Best==-FLT_MAX)
	{
		// In dense smoke, allow retracing physically walked locations, not remote guesses.
		for (int32 I=Visited.Num()-2, Count=0; I>=0 && Count<3; --I)
		{
			if (FVector::DistSquared(Visited[I],Feet)>FMath::Square(Radius)) continue;
			++Count;
			Consider(Visited[I],nullptr,true);
		}
	}
	if (Best==-FLT_MAX) return false;
	SearchTarget=Selected;bHasSearchTarget=true;SearchDeadline=Clock+FMath::Max(3.f,MaxSearchLegSeconds);
	SearchDoor=SelectedDoor;
	if (SelectedDoor)
	{
		SearchDoorSide=FVector::DotProduct(Feet-SelectedDoor->GetPortalFloorPoint(0),SelectedDoor->GetActorForwardVector())<0.0 ? -1.0 : 1.0;
		if (SelectedDoor->IsUserInReach(NPC)) return false;
	}
	Out=SearchTarget;return true;
}
void UYUFSExitKnowledgeComponent::TickSearchLook(float DeltaTime)
{
	auto* NPC=OwnerNPC();if (!NPC || !bSearching || bHasSearchTarget || NPC->bUseExternalMotionDriver || NPC->bUseExternalNavigationDriver) return;
	FRotator Look=NPC->GetActorRotation();Look.Yaw+=60.f*DeltaTime;NPC->SetActorRotation(Look);
}
FString UYUFSExitKnowledgeComponent::GetKnowledgeDiagnostics() const
{
	FString Result=FString::Printf(TEXT("knownExits=%d knownDoors=%d breadcrumbs=%d searchingExit=%d searchTarget=%d searchQueries=%d"),GetKnownExitCount(),GetKnownDoorCount(),Visited.Num(),bSearching,bHasSearchTarget,SearchQueryCount);
	for (const auto& Entry:Exits) if (Entry.Actor.IsValid()) Result+=FString::Printf(TEXT(" [%s:%s%s]"),*Entry.Actor->ExitID.ToString(),*Entry.Source.ToString(),Entry.bFamiliar?TEXT("/Entry"):TEXT(""));
	return Result;
}
