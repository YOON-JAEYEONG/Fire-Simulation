#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/YUFSObservation.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "NPC/Integration/YUFSBelongingsProp.h"
#include "NPC/Integration/YUFSTeamIntegrationComponent.h"
#include "NPC/YUFSEvacuationNPC.h"

struct FYUFSBelongingsReturnTestAccess
{
	static void SetReturning(AYUFSEvacuationNPC& Npc,FVector Target)
	{
		Npc.BelongingsReturn=AYUFSEvacuationNPC::EBelongingsReturn::Returning;
		Npc.BelongingsReturnTarget=Target;
		Npc.BelongingsReturnRemaining=10.f;
		Npc.BelongingsPickupElapsed=0.f;
		Npc.CurrentAction=EYUFSAction::EvacuateToNearestExit;
	}
	static bool IsCandidate(const AYUFSEvacuationNPC& Npc) { return Npc.BelongingsReturn==AYUFSEvacuationNPC::EBelongingsReturn::Candidate; }
	static void SetID(AYUFSEvacuationNPC& Npc,int32 ID) { Npc.StableNPCId=ID; }
	static void Evaluate(AYUFSEvacuationNPC& Npc,const FYUFSNPCObservation& O,float Dt) { Npc.EvaluateBelongingsReturn(O,Dt); }
	static bool Drive(AYUFSEvacuationNPC& Npc,float Dt) { return Npc.DriveBelongingsReturn(Dt); }
	static void Publish(AYUFSEvacuationNPC& Npc,const FYUFSNPCObservation& O) { Npc.PublishTeamDirectives(O); }
	static void ShowBag(AYUFSEvacuationNPC& Npc,FVector Floor)
	{ Npc.ApplyPersonalBagVisual(true,false,Floor,true); }
	static const AYUFSBelongingsProp* GetBag(const AYUFSEvacuationNPC& Npc) { return Npc.PersonalBag.Get(); }
};

namespace
{
struct FBagTestRoom
{
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
	AYUFSEvacuationNPC* Npc=World->SpawnActor<AYUFSEvacuationNPC>();
	FYUFSNPCObservation O;
	FBagTestRoom()
	{
		auto* Floor=World->SpawnActor<AActor>();
		auto* Slab=NewObject<UBoxComponent>(Floor); Floor->SetRootComponent(Slab);
		Slab->SetBoxExtent(FVector(550,550,10)); Slab->SetCollisionProfileName(TEXT("BlockAll"));
		Slab->RegisterComponent(); Floor->SetActorLocation(FVector(0,0,-10));
		Npc->SetActorLocation(FVector(0,0,Npc->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2));
		auto* Mind=Npc->GetBehaviorStateMachine(); Mind->Config=NewObject<UYUFSBehaviorConfig>(Mind);
		O.NearbyHeat=.9f; Mind->TickStateMachine(.1f,O); // Legitimate immediate commitment.
		O.NearbyHeat=0.f; O.CurrentState=EYUFSBehaviorState::Evacuating;
	}
	~FBagTestRoom() { World->DestroyWorld(false); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSBagPickupTest,"YUFS.NPC.BelongingsReturn.PickupDoesNotReplaceEvacuationIntent",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSBagPickupTest::RunTest(const FString&)
{
	FBagTestRoom F;
	using Access=FYUFSBelongingsReturnTestAccess;
	const FVector BagFloor(65,0,0);
	Access::ShowBag(*F.Npc,BagFloor);
	const auto* Bag=Access::GetBag(*F.Npc);
	if (!TestNotNull(TEXT("The remembered bag has an actual floor actor"),Bag)) return false;
	TestEqual(TEXT("Bag sits at the remembered pickup position"),Bag->GetActorLocation(),BagFloor);
	TArray<UStaticMeshComponent*> Parts;
	Bag->GetComponents<UStaticMeshComponent>(Parts);
	TestTrue(TEXT("Bag model has multiple visible details"),Parts.Num()>=5);
	for (const auto* Part:Parts)
		TestFalse(TEXT("No visual piece blocks the walking capsule"),Part->GetCollisionEnabled()!=ECollisionEnabled::NoCollision);
	const auto BeforePickup=F.Npc->BuildTimelineSnapshot();
	Access::SetReturning(*F.Npc,FVector(65,0,0));
	Access::Publish(*F.Npc,F.O);
	TestEqual(TEXT("The return destination is the remembered location, not an exit"),F.Npc->GetTeamIntegrationComponent()->GetNavigationDirective().DestinationHint,FVector(65,0,0));
	TestEqual(TEXT("Walk back before playing the pickup gesture"),F.Npc->GetTeamIntegrationComponent()->GetMotionDirective().Semantic,EYUFSMotionSemantic::Walk);
	auto* Team = F.Npc->GetTeamIntegrationComponent();
	TestFalse(TEXT("A personal bag does not need a shared reservation"),Team->GetInteractionDirective().bRequiresReservation);
	auto Opportunities = Team->GetInteractionOpportunities();
	Opportunities.bDoorActionRequired = true;
	Opportunities.DoorStableId = TEXT("RoomDoor");
	Team->SubmitInteractionOpportunities(Opportunities);
	Access::Publish(*F.Npc,F.O);
	TestEqual(TEXT("The room door is still an independent interaction"),Team->GetInteractionDirective().Goal,EYUFSInteractionGoal::OpenDoor);
	TestTrue(TEXT("Door reservation is still required while returning"),Team->GetInteractionDirective().bRequiresReservation);
	Opportunities.bDoorActionRequired = false;
	Opportunities.DoorStableId = NAME_None;
	Team->SubmitInteractionOpportunities(Opportunities);
	TestTrue(TEXT("Pickup is a stationary sub-action at the reachable location"),Access::Drive(*F.Npc,.3f));
	TestTrue(TEXT("Physical approach enters pickup"),F.Npc->IsPickingUpBelongings());
	TestNull(TEXT("The bag waits on the floor until the gesture finishes"),Bag->GetAttachParentActor());
	TestEqual(TEXT("Show the existing gathering gesture"),F.Npc->GetDisplayedAction(),EYUFSAction::GatherBelongings);
	Access::Publish(*F.Npc,F.O);
	TestFalse(TEXT("Pickup does not publish another movement target"),F.Npc->GetTeamIntegrationComponent()->GetNavigationDirective().bHasDestination);
	TestFalse(TEXT("Completed pickup gives movement ownership back to evacuation"),Access::Drive(*F.Npc,.6f));
	TestEqual(TEXT("Return never overwrites the committed evacuation intent"),F.Npc->GetCurrentIntent(),EYUFSIntent::CommitEvac);
	TestEqual(TEXT("The former evacuation behavior remains active"),F.Npc->GetLastAction(),EYUFSAction::EvacuateToNearestExit);
	TestFalse(TEXT("The personal destination is cleared"),F.Npc->GetTeamIntegrationComponent()->GetInteractionOpportunities().bBelongingsKnown);
	TestTrue(TEXT("Successful pickup attaches the same actor instead of spawning another prop"),Bag->GetAttachParentActor()==F.Npc);
	const auto AfterPickup=F.Npc->BuildTimelineSnapshot();
	TestTrue(TEXT("The timeline captures a carried bag"),AfterPickup.bHasPersonalBag && AfterPickup.bCarryingPersonalBag);
	F.Npc->ApplyTimelineSnapshot(BeforePickup);
	TestTrue(TEXT("Rewinding places the bag back on the same floor"),!Bag->GetAttachParentActor() && Bag->GetActorLocation().Equals(BagFloor));
	F.Npc->ApplyTimelineSnapshot(AfterPickup);
	TestTrue(TEXT("Seeking after the pickup reattaches the same bag"),Bag->GetAttachParentActor()==F.Npc);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSBagReturnSafetyTest,"YUFS.NPC.BelongingsReturn.OptionalReturnCancelsOnRiskOrTimeout",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSBagReturnSafetyTest::RunTest(const FString&)
{
	FBagTestRoom F;
	using Access=FYUFSBelongingsReturnTestAccess;
	Access::ShowBag(*F.Npc,FVector(400,0,0));
	Access::SetReturning(*F.Npc,FVector(400,0,0));
	F.O.SmokeDensityAtSelf=.31f; Access::Evaluate(*F.Npc,F.O,.1f);
	TestFalse(TEXT("Smoke cancels this optional return, not evacuation"),F.Npc->IsReturningForBelongings());
	const auto* Bag=Access::GetBag(*F.Npc);
	if (TestNotNull(TEXT("Cancellation leaves the same bag in the world"),Bag))
		TestTrue(TEXT("The abandoned bag remains visible on the floor"),!Bag->IsHidden() && !Bag->GetAttachParentActor());
	F.O.SmokeDensityAtSelf=0.f; Access::Evaluate(*F.Npc,F.O,1.f);
	TestFalse(TEXT("The NPC cannot retry the return after cancellation"),F.Npc->IsReturningForBelongings() || Access::IsCandidate(*F.Npc));
	Access::SetReturning(*F.Npc,FVector(400,0,0));
	Access::Evaluate(*F.Npc,F.O,11.f);
	TestFalse(TEXT("The return has a hard time budget"),F.Npc->IsReturningForBelongings());
	Access::SetReturning(*F.Npc,FVector(400,0,0));
	F.Npc->NotifyEpisodeFinished(EYUFSTerminalReason::ReachedExit);
	TestFalse(TEXT("Exit completion permanently ends the return"),F.Npc->IsReturningForBelongings());
	return true;
}

#endif
