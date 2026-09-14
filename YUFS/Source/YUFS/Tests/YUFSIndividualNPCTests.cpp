#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "NPC/Perception/YUFSNPCPerceptionComponent.h"
#include "NPC/Navigation/YUFSLocalMovementComponent.h"
#include "Core/YUFSObservation.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"

namespace
{
UYUFSBehaviorStateMachine* Mind(int32 Seed)
{
	auto* S=NewObject<UYUFSBehaviorStateMachine>();
	S->Config=NewObject<UYUFSBehaviorConfig>(S);
	S->InitializePersonality(Seed);
	return S;
}
void Advance(UYUFSBehaviorStateMachine* S,FYUFSNPCObservation O,float Seconds)
{
	for (int32 I=0; I<FMath::RoundToInt(Seconds*10); ++I) S->TickStateMachine(0.1f,O);
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSIndividualAlarmTest,"YUFS.NPC.Individual.AlarmDiversityAndDeterminism",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSIndividualAlarmTest::RunTest(const FString&)
{
	int32 Early=0,Late=0,Waiting=0;
	FYUFSNPCObservation O; O.bAlarmSounding=true;
	for (int32 I=0;I<100;++I)
	{
		auto* S=Mind(I); auto* Copy=Mind(I);
		TestEqual(TEXT("Same seed gives the same personality"),S->GetAlarmTrust(),Copy->GetAlarmTrust());
		Advance(S,O,12.f); Early+=S->HasCommittedToEvacuation();
		Advance(S,O,60.f); Late+=S->HasCommittedToEvacuation(); Waiting+=!S->HasCommittedToEvacuation();
	}
	TestTrue(TEXT("Only part of the population commits soon after an alarm"),Early>5 && Early<60);
	TestTrue(TEXT("Some verify the alarm longer"),Late>Early);
	TestTrue(TEXT("Sceptics are not forced to depart by a universal timer"),Waiting>5);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSIndividualEvidenceTest,"YUFS.NPC.Individual.HeatSmokePeersAndIncapacitation",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSIndividualEvidenceTest::RunTest(const FString&)
{
	FYUFSNPCObservation O;
	auto* Heat=Mind(9); O.NearbyHeat=0.9f;
	Advance(Heat,O,0.1f);
	TestEqual(TEXT("Local high heat triggers evacuation without alarm"),Heat->GetCurrentState(),EYUFSBehaviorState::Evacuating);
	TestEqual(TEXT("Reason is heat"),Heat->GetDecisionCue(),EYUFSEvacuationCue::Heat);
	auto* Smoke=Mind(12); O={}; O.SmokeInFrontNormalized=0.65f;
	Advance(Smoke,O,1.f);
	TestTrue(TEXT("Strong visible smoke skips long preparation"),Smoke->HasCommittedToEvacuation());
	auto* Peer=Mind(40); O={}; O.bHeardPeerWarning=true;
	Advance(Peer,O,15.f);
	TestTrue(TEXT("A warning can trigger evacuation without own exposure"),Peer->HasCommittedToEvacuation());
	TestEqual(TEXT("Warning is not relabelled as own perception"),Peer->GetDecisionCue(),EYUFSEvacuationCue::PeerWarning);
	TestFalse(TEXT("Hearsay cannot propagate as an eyewitness"),Peer->HasRecentDirectEvidence());
	auto* Crowd=Mind(40); O={}; O.NearbyNPCCount=3; O.NearbyEvacuatingRatio=0.8f;
	Advance(Crowd,O,15.f); TestTrue(TEXT("Observed crowd movement influences decision"),Crowd->HasCommittedToEvacuation());
	auto* Quiet=Mind(1); O={}; Advance(Quiet,O,120.f);
	TestEqual(TEXT("Elapsed time without cues does not trigger evacuation"),Quiet->GetCurrentState(),EYUFSBehaviorState::Normal);
	auto* Down=Mind(1); Down->Config->SmokeExposureAccumRate=1.f;
	O.SmokeDensityAtSelf=1.f; Advance(Down,O,2.f); O.NearbyHeat=1.f; Advance(Down,O,1.f);
	TestEqual(TEXT("Heat cannot revive an incapacitated person"),Down->GetCurrentState(),EYUFSBehaviorState::Incapacitated);
	TArray<float> Features; O.FillFloatArray(Features);
	TestEqual(TEXT("Existing model input width is preserved"),Features.Num(),FYUFSNPCObservation::FeatureCount);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSTrafficOrderTest,"YUFS.NPC.Navigation.Traffic.StableYieldAndFloors",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSTrafficOrderTest::RunTest(const FString&)
{
	using T=UYUFSLocalMovementComponent;
	const FVector A(0,0,0), B(150,0,0), Right(1,0,0), Left(-1,0,0);
	TestTrue(TEXT("Head-on encounter is predicted before contact"),T::TrajectoriesConflict(A,Right,40,B,Left,40,200));
	TestFalse(TEXT("Parallel separate lanes do not form one queue"),T::TrajectoriesConflict(A,Right,40,FVector(0,250,0),Right,40,200));
	TestFalse(TEXT("NPC on another floor is not a blocker"),T::TrajectoriesConflict(A,Right,40,B+FVector(0,0,300),Left,40,200));
	TestFalse(TEXT("Moving away from a nearby stationary person does not trigger yielding"),T::TrajectoriesConflict(A,Left,40,FVector(70,0,0),FVector::ZeroVector,40,200));
	TestTrue(TEXT("Following NPC yields to the front regardless of ID"),T::ShouldYieldTo(A,Right,1,B,Right,100));
	TestFalse(TEXT("Front never yields to its own follower"),T::ShouldYieldTo(B,Right,100,A,Right,1));
	for (int32 I=0;I<50;++I)
	{
		TestFalse(TEXT("Priority stays fixed across updates"),T::ShouldYieldTo(A,Right,10,B,Left,20));
		TestTrue(TEXT("Only the other participant yields"),T::ShouldYieldTo(B,Left,20,A,Right,10));
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSPersonalKnowledgeTest,"YUFS.NPC.Navigation.Hazard.PersonalKnowledgeAndMemory",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSPersonalKnowledgeTest::RunTest(const FString&)
{
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
	auto* Actor=World->SpawnActor<ACharacter>();
	auto* P=NewObject<UYUFSNPCPerceptionComponent>(Actor); P->RegisterComponent(); P->Config=NewObject<UYUFSPerceptionConfig>(P);
	P->Config->VisionRayCount=1; P->Config->VisionRange=500;
	Actor->SetActorLocation(FVector(100,100,100));
	auto Grid=MakeShared<FYUFSHazardGrid,ESPMode::ThreadSafe>(); Grid->Dimensions=FIntVector(20,10,10);
	Grid->Density.Init(0,2000); Grid->Temperature.Init(0,2000);
	for (int32 X=5;X<8;++X) for (int32 Y=0;Y<10;++Y) for (int32 Z=0;Z<10;++Z) Grid->Temperature[(X*10+Y)*10+Z]=255;
	FYUFSHazardSnapshot Raw; Raw.Grid=Grid; Raw.GridToWorld=FTransform(FQuat::Identity,FVector::ZeroVector,FVector(40)); Raw.Status=EYUFSHazardDataStatus::Ready; Raw.Frame=10;
	const FVector Hot(240,100,100);
	TestEqual(TEXT("Unseen heat cannot leak into path costs"),P->RestrictToKnowledge(Raw).Sample(Hot).Heat,0.f);
	P->UpdateFromSnapshot(Raw,0.f);
	TestTrue(TEXT("Heat-only region is sensed"),P->GetHeatInSight()>0.5f);
	TestTrue(TEXT("Observed heat is recorded"),P->GetKnownCellCount()>0);
	const auto Old=P->RestrictToKnowledge(Raw);
	Actor->SetActorLocation(FVector(700,100,100)); // face away; nearby probes no longer reach the source
	P->UpdateFromSnapshot(Raw,2.f);
	TestTrue(TEXT("Observed hazards remain after looking away"),P->GetKnownCellCount()>0);
	P->UpdateFromSnapshot(Raw,40.f);
	TestEqual(TEXT("Old unobserved records expire"),P->GetKnownCellCount(),0);
	TestTrue(TEXT("An in-flight path keeps its immutable previous knowledge"),Old.KnownCells->Num()>0);
	World->DestroyWorld(false); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSCueOcclusionTest,"YUFS.NPC.Individual.WallsOccludeHeatAndSmoke",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSCueOcclusionTest::RunTest(const FString&)
{
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
	auto* Actor=World->SpawnActor<ACharacter>(); Actor->SetActorLocation(FVector(100,100,100));
	auto* P=NewObject<UYUFSNPCPerceptionComponent>(Actor); P->RegisterComponent(); P->Config=NewObject<UYUFSPerceptionConfig>(P);
	P->Config->VisionRayCount=1; P->Config->VisionRange=500;
	auto* Wall=World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Wall);
	Wall->SetRootComponent(Box); Box->SetBoxExtent(FVector(15,1000,1000)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent(); Wall->SetActorLocation(FVector(180,100,100));
	auto Grid=MakeShared<FYUFSHazardGrid,ESPMode::ThreadSafe>(); Grid->Dimensions=FIntVector(20,10,10); Grid->Density.Init(0,2000); Grid->Temperature.Init(0,2000);
	for (int32 X=6;X<15;++X) for (int32 Y=0;Y<10;++Y) for (int32 Z=0;Z<10;++Z) { Grid->Density[(X*10+Y)*10+Z]=255; Grid->Temperature[(X*10+Y)*10+Z]=255; }
	FYUFSHazardSnapshot Raw; Raw.Grid=Grid; Raw.GridToWorld=FTransform(FQuat::Identity,FVector::ZeroVector,FVector(40)); Raw.Status=EYUFSHazardDataStatus::Ready;
	P->UpdateFromSnapshot(Raw,0.f);
	TestEqual(TEXT("Wall blocks heat ahead"),P->GetHeatInSight(),0.f);
	TestEqual(TEXT("Wall blocks nearby heat probes too"),P->GetNearbyHeat(),0.f);
	TestEqual(TEXT("Wall blocks smoke ahead"),P->GetSmokeInFrontNormalized(),0.f);
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	P->UpdateFromSnapshot(Raw,1.f);
	TestTrue(TEXT("Opening the view reveals smoke and heat"),P->GetHeatInSight()>0.5f && P->GetSmokeInFrontNormalized()>0.5f);
	World->DestroyWorld(false); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSRecoveryClearanceTest,"YUFS.NPC.Navigation.Traffic.RecoveryChecksCapsuleAndFloor",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSRecoveryClearanceTest::RunTest(const FString&)
{
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
	auto* NPC=World->SpawnActor<AYUFSEvacuationNPC>();
	const float Half=NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	NPC->SetActorLocation(FVector(0,0,Half+2));
	auto MakeBox=[&](FVector P,FVector Extent)
	{
		auto* A=World->SpawnActor<AActor>(); auto* B=NewObject<UBoxComponent>(A); A->SetRootComponent(B);
		B->SetBoxExtent(Extent); B->SetCollisionProfileName(TEXT("BlockAll")); B->RegisterComponent(); A->SetActorLocation(P); return B;
	};
	auto* Floor=MakeBox(FVector(0,0,-10),FVector(500,500,10));
	const auto* Local=NPC->GetLocalMovement();
	TestTrue(TEXT("A clear, supported physical back step is permitted"),Local->IsPhysicalCorridorClear(FVector(-140,0,0)));
	auto* Wall=MakeBox(FVector(-70,0,100),FVector(10,100,100));
	TestFalse(TEXT("A wall between two free endpoints blocks the entire recovery sweep"),Local->IsPhysicalCorridorClear(FVector(-140,0,0)));
	Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TestFalse(TEXT("Recovery never jumps to another floor"),Local->IsPhysicalCorridorClear(FVector(-140,0,300)));
	Floor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TestFalse(TEXT("A clear capsule sweep without ground support is refused"),Local->IsPhysicalCorridorClear(FVector(-140,0,0)));
	World->DestroyWorld(false); return true;
}
struct FYUFSCrowdIntegrationAccess
{
	static void SetRoute(AYUFSEvacuationNPC* NPC,FVector Goal)
	{
		auto* Nav=NPC->GetNavigator(); Nav->CurrentPath={NPC->GetActorLocation(),Goal};
		Nav->CurrentWaypointIndex=1; Nav->NavigationStatus=EYUFSNavigationStatus::Moving;
	}
};
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSTwoAgentYieldTest,"YUFS.NPC.Navigation.Traffic.TwoNPCsYieldAndRelease",
	EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSTwoAgentYieldTest::RunTest(const FString&)
{
	UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
	auto* A=World->SpawnActor<AYUFSEvacuationNPC>(); A->SetActorLocation(FVector(-100,55,100));
	auto* B=World->SpawnActor<AYUFSEvacuationNPC>(); B->SetActorLocation(FVector(-100,-55,100));
	const FVector Goal(300,0,100);
	FYUFSCrowdIntegrationAccess::SetRoute(A,Goal); FYUFSCrowdIntegrationAccess::SetRoute(B,Goal);
	const FVector DA=(Goal-A->GetActorLocation()).GetSafeNormal2D(), DB=(Goal-B->GetActorLocation()).GetSafeNormal2D();
	const FVector StepA=A->GetLocalMovement()->ResolveDirection(DA,0.1f,0);
	const FVector StepB=B->GetLocalMovement()->ResolveDirection(DB,0.1f,0);
	TestTrue(TEXT("Converging actors never both yield or both enter"),StepA.IsNearlyZero()!=StepB.IsNearlyZero());
	auto* Winner=StepA.IsNearlyZero() ? B : A; auto* Waiting=Winner==A ? B : A;
	Winner->SetActorLocation(FVector(600,0,100));
	const FVector Released=Waiting->GetLocalMovement()->ResolveDirection((Goal-Waiting->GetActorLocation()).GetSafeNormal2D(),0.1f,0);
	TestFalse(TEXT("Waiting actor resumes once the conflict clears"),Released.IsNearlyZero());
	TestEqual(TEXT("Yield state is released"),Waiting->GetLocalMovement()->GetState(),EYUFSLocalMovementState::Following);
	World->DestroyWorld(false); return true;
}
#endif
