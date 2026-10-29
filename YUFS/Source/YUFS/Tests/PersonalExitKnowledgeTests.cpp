#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "Level/YUFSLevelDataManager.h"
#include "Level/YUFSExitPoint.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "NPC/Perception/YUFSNPCPerceptionComponent.h"
#include "NPC/Navigation/YUFSExitKnowledgeComponent.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "NPC/Social/YUFSSocialInfluenceComponent.h"
#include "NPC/Integration/YUFSTeamIntegrationComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

struct FYUFSPersonalExitTestAccess
{
    static void Wire(AYUFSEvacuationNPC* N, AYUFSLevelDataManager* M) { N->LevelDataMgr=M; }
    static FVector Resolve(AYUFSEvacuationNPC* N,EYUFSAction Action) { return N->ResolveNavigationTarget(Action); }
    static bool TryResolve(AYUFSEvacuationNPC* N,EYUFSAction Action,FVector& Out) { return N->TryResolveNavigationTarget(Action,Out); }
    static void Execute(AYUFSEvacuationNPC* N) { N->CurrentAction=EYUFSAction::EvacuateToNearestExit;N->ExecuteCurrentAction(.1f); }
    static void ObservePeer(AYUFSEvacuationNPC* Observer,AYUFSEvacuationNPC* Peer,FVector PrivateGoal)
    {
        Observer->GetSocialComponent()->NearbyNPCs.Add(Peer);
        Peer->CurrentAction=EYUFSAction::EvacuateToNearestExit;
        Peer->GetNavigator()->NavigationStatus=EYUFSNavigationStatus::Moving;
        Peer->GetNavigator()->RequestedDestination=PrivateGoal;
        Peer->GetCharacterMovement()->Velocity=FVector(100,0,0);
    }
    static void SetFollower(AYUFSEvacuationNPC* Peer) { Peer->CurrentAction=EYUFSAction::FollowCrowd; }
};

namespace
{
struct FPersonalExitFixture
{
    UWorld* World;
    AYUFSLevelDataManager* Manager;
    AYUFSExitPoint* Exit;
    AYUFSExitPoint* Entry;
    AYUFSEvacuationNPC* NPC;
    FPersonalExitFixture()
    {
        const auto Settings=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
        World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Settings);
        Exit=World->SpawnActor<AYUFSExitPoint>(FVector(2000,0,0),FRotator::ZeroRotator); Exit->ExitID=TEXT("UnknownExit");
        Entry=World->SpawnActor<AYUFSExitPoint>(FVector::ZeroVector,FRotator::ZeroRotator);Entry->ExitID=TEXT("Entry");
        Manager=World->SpawnActor<AYUFSLevelDataManager>(); Manager->DispatchBeginPlay();
        NPC=World->SpawnActor<AYUFSEvacuationNPC>(FVector(100,100,90),FRotator::ZeroRotator);
        NPC->GetBehaviorStateMachine()->Config=NewObject<UYUFSBehaviorConfig>(NPC);
        NPC->GetNPCPerceptionComponent()->Config=NewObject<UYUFSPerceptionConfig>(NPC);
        FYUFSPersonalExitTestAccess::Wire(NPC,Manager);
    }
    ~FPersonalExitFixture() { World->DestroyWorld(false); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSNoGlobalExitKnowledgeTest,"YUFS.NPC.Navigation.PersonalExits.UnseenExitIsNotAutomaticallyKnown",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSNoGlobalExitKnowledgeTest::RunTest(const FString&)
{
    FPersonalExitFixture F; FVector Goal;
    TestFalse(TEXT("A new NPC cannot select a remote unobserved map exit"),F.NPC->TryGetNearestKnownExit(Goal));
    TestTrue(TEXT("Familiar action cannot manufacture the nearest exit as prior knowledge"),FYUFSPersonalExitTestAccess::Resolve(F.NPC,EYUFSAction::EvacuateToFamiliarExit).IsZero());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSAlarmNoExitKnowledgeTest,"YUFS.NPC.Navigation.PersonalExits.AlarmDoesNotRevealAllExits",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSAlarmNoExitKnowledgeTest::RunTest(const FString&)
{
    FPersonalExitFixture F; FVector Goal;
    F.NPC->OnCommReceived(EYUFSCommType::AlarmOnly,F.NPC->GetActorLocation(),500.f,FVector::ZeroVector);
    TestTrue(TEXT("The alarm is still heard"),F.NPC->IsAlarmSounding());
    TestFalse(TEXT("Hearing a general alarm does not reveal the remote exit location"),F.NPC->TryGetNearestKnownExit(Goal));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSAuthoredEntryMemoryTest,"YUFS.NPC.Navigation.PersonalExits.MarkedEntryAndOriginGoal",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSAuthoredEntryMemoryTest::RunTest(const FString&)
{
    FPersonalExitFixture F;F.Entry->bIsFamiliarEntry=true;
    auto* Memory=F.NPC->GetExitKnowledge();Memory->InitializeMemory();
    TestEqual(TEXT("Prior knowledge contains only the marked entry, not every exit"),Memory->GetKnownExitCount(),1);
    FVector Goal(99);TestTrue(TEXT("An origin entry remains a valid known route target"),FYUFSPersonalExitTestAccess::TryResolve(F.NPC,EYUFSAction::EvacuateToFamiliarExit,Goal));
    TestTrue(TEXT("The remembered entry can be at world origin"),Goal.IsZero());
    FYUFSPersonalExitTestAccess::Execute(F.NPC);
    TestEqual(TEXT("Native movement submits origin instead of treating it as absent"),F.NPC->GetNavigator()->GetLastFailure(),EYUFSNavigationFailure::NoNavigationData);
    Memory->ResetMemory();Memory->bRememberMarkedEntry=false;Memory->InitializeMemory();
    TestEqual(TEXT("A visitor can explicitly lack entrance memory"),Memory->GetKnownExitCount(),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSExitLandmarkSightTest,"YUFS.NPC.Navigation.PersonalExits.WallsFogAndIndividualDiscovery",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSExitLandmarkSightTest::RunTest(const FString&)
{
    FPersonalExitFixture F;F.NPC->SetActorLocation(FVector(1500,100,90));
    auto* Other=F.World->SpawnActor<AYUFSEvacuationNPC>(FVector(1500,100,90),FRotator(0,180,0));
    Other->GetNPCPerceptionComponent()->Config=NewObject<UYUFSPerceptionConfig>(Other);FYUFSPersonalExitTestAccess::Wire(Other,F.Manager);
    auto Data=MakeShared<FYUFSHazardGrid,ESPMode::ThreadSafe>();Data->Dimensions=FIntVector(120,40,40);
    Data->Density.Init(0,120*40*40);Data->Temperature.Init(0,120*40*40);
    FYUFSHazardSnapshot Field;Field.Grid=Data;Field.GridToWorld=FTransform(FQuat::Identity,FVector::ZeroVector,FVector(20));Field.Status=EYUFSHazardDataStatus::Ready;
    auto* Memory=F.NPC->GetExitKnowledge();Memory->ObserveExits(Field,true);
    Other->GetExitKnowledge()->ObserveExits(Field,true);
    TestEqual(TEXT("An actually visible exit is learned"),Memory->GetKnownExitCount(),1);
    TestEqual(TEXT("Looking the other way does not share another NPC's knowledge"),Other->GetExitKnowledge()->GetKnownExitCount(),0);
    Memory->ResetMemory();
    auto* Wall=F.World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Wall);Wall->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(10,500,400));Box->SetCollisionProfileName(TEXT("BlockAll"));Box->RegisterComponent();Wall->SetActorLocation(FVector(1750,100,200));
    Memory->ObserveExits(Field,true);TestEqual(TEXT("A wall prevents exit recognition"),Memory->GetKnownExitCount(),0);Wall->Destroy();
    for(int X=80;X<95;++X)for(int Y=0;Y<40;++Y)for(int Z=0;Z<40;++Z)Data->Density[(X*40+Y)*40+Z]=255;
    Memory->ObserveExits(Field,true);TestEqual(TEXT("Dense intervening smoke prevents new exit recognition"),Memory->GetKnownExitCount(),0);
    Memory->ObserveExits(Field,false);TestEqual(TEXT("Preloaded future fire data does not hide an exit before ignition"),Memory->GetKnownExitCount(),1);Memory->ResetMemory();
    Data->Density.Init(0,Data->Density.Num());Memory->ObserveExits(Field,true);
    F.NPC->SetActorRotation(FRotator(0,180,0));Memory->ObserveExits(Field,true);
    TestEqual(TEXT("Remembering a seen exit does not require continuously looking at it"),Memory->GetKnownExitCount(),1);
    F.Exit->Destroy();TestEqual(TEXT("Destroyed exits cannot remain usable goals"),Memory->GetKnownExitCount(),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSExitOfficialGuidanceTest,"YUFS.NPC.Navigation.PersonalExits.OfficialGuidanceValidatesLocationAndRadius",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSExitOfficialGuidanceTest::RunTest(const FString&)
{
    FPersonalExitFixture F;auto* M=F.NPC->GetExitKnowledge();
    F.NPC->OnCommReceived(EYUFSCommType::StaffGuidance,FVector(10000),10.f,F.Exit->GetActorLocation());
    TestEqual(TEXT("Out of range instructions do not teach an exit"),M->GetKnownExitCount(),0);
    F.NPC->OnCommReceived(EYUFSCommType::StaffGuidance,F.NPC->GetActorLocation(),100.f,FVector(99999));
    TestEqual(TEXT("An arbitrary non-exit coordinate is not promoted to an exit"),M->GetKnownExitCount(),0);
    F.NPC->OnCommReceived(EYUFSCommType::StaffGuidance,F.NPC->GetActorLocation(),100.f,F.Exit->GetActorLocation());
    TestEqual(TEXT("Specific nearby official instruction can teach an unseen exit"),M->GetKnownExitCount(),1);
    F.NPC->OnCommReceived(EYUFSCommType::StaffGuidance,F.NPC->GetActorLocation(),100.f,FVector::ZeroVector);
    TestTrue(TEXT("A valid registered origin exit is not rejected as absent guidance"),M->KnowsExitLocation(FVector::ZeroVector));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSExitPeerKnowledgeTest,"YUFS.NPC.Navigation.PersonalExits.PeerPositionIsNotPrivateExitKnowledge",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSExitPeerKnowledgeTest::RunTest(const FString&)
{
    FPersonalExitFixture F;F.NPC->SetActorLocation(FVector(220,100,90));
    F.NPC->GetExitKnowledge()->InitiallyKnownExitIDs={F.Exit->ExitID};F.NPC->GetExitKnowledge()->InitializeMemory();
    auto* Listener=F.World->SpawnActor<AYUFSEvacuationNPC>(FVector(100,100,90),FRotator::ZeroRotator);
    Listener->GetNPCPerceptionComponent()->Config=NewObject<UYUFSPerceptionConfig>(Listener);FYUFSPersonalExitTestAccess::Wire(Listener,F.Manager);
    FYUFSPersonalExitTestAccess::ObservePeer(Listener,F.NPC,F.Exit->GetActorLocation());
    FVector Follow;TestTrue(TEXT("A visible evacuee can provide a local following target"),Listener->GetSocialComponent()->TryGetObservedEvacuationDestination(Follow));
    TestTrue(TEXT("Following uses the nearby person's feet rather than their remote goal"),Follow.Equals(F.NPC->GetActorLocation()-FVector(0,0,F.NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())));
    TestEqual(TEXT("Merely following does not copy the leader's exit memory"),Listener->GetExitKnowledge()->GetKnownExitCount(),0);
    FYUFSPersonalExitTestAccess::SetFollower(F.NPC);
    TestFalse(TEXT("Followers cannot become each other's circular escape leader"),Listener->GetSocialComponent()->TryGetObservedEvacuationDestination(Follow));
    TestTrue(TEXT("Explicit close guidance shares a known exit without requiring hazard data"),Listener->ReceiveExitGuidance(F.NPC));
    TestTrue(TEXT("The recipient now remembers the stated registered exit"),Listener->GetExitKnowledge()->KnowsExitLocation(F.Exit->GetActorLocation()));
    Listener->GetExitKnowledge()->ResetMemory();F.NPC->SetActorLocation(FVector(1000,100,90));
    TestFalse(TEXT("A remote NPC cannot silently transfer knowledge"),Listener->ReceiveExitGuidance(F.NPC));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSExitSearchContractTest,"YUFS.NPC.Navigation.PersonalExits.SearchBackoffAndExplicitRouteContract",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSExitSearchContractTest::RunTest(const FString&)
{
    FPersonalExitFixture F;auto* M=F.NPC->GetExitKnowledge();FVector Goal;
    for(int I=0;I<100;++I)TestFalse(TEXT("Missing NavMesh cannot invent a search waypoint"),M->TryGetSearchTarget(Goal));
    TestEqual(TEXT("Repeated target reads do not cause path queries"),M->GetSearchQueryCount(),0);
    TestTrue(TEXT("The NPC explicitly remains in search/look mode"),M->IsSearchingForExit());
    auto* Team=F.NPC->GetTeamIntegrationComponent();FYUFSBehaviorDecision D;D.Behavior=EYUFSHighLevelBehavior::EvacuateNearest;D.LegacyAction=EYUFSAction::EvacuateToNearestExit;
    Team->PublishDecision(1,D,EYUFSIntent::CommitEvac,EYUFSBehaviorState::Evacuating,FVector::ZeroVector,false,FYUFSCognitiveState(),false,true);
    TestTrue(TEXT("Unknown target cannot become an origin movement command"),Team->GetNavigationDirective().Goal==EYUFSNavigationGoal::None && !Team->GetNavigationDirective().bHasDestination);
    Team->PublishDecision(1,D,EYUFSIntent::CommitEvac,EYUFSBehaviorState::Evacuating,FVector(400,100,0),false,FYUFSCognitiveState(),true,true);
    TestTrue(TEXT("Search waypoints are identified as investigation, not certified safe exits"),Team->GetNavigationDirective().Goal==EYUFSNavigationGoal::InvestigateTarget && Team->GetNavigationDirective().bSearchingForExit);
    Team->PublishDecision(1,D,EYUFSIntent::CommitEvac,EYUFSBehaviorState::Evacuating,FVector::ZeroVector,true,FYUFSCognitiveState(),true,false);
    TestTrue(TEXT("A known origin exit remains distinguishable from no destination"),Team->GetNavigationDirective().Goal==EYUFSNavigationGoal::SafeExit && Team->GetNavigationDirective().bHasDestination);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSExitSetupResetTest,"YUFS.NPC.Navigation.PersonalExits.PlacementResetAndIncompleteSight",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSExitSetupResetTest::RunTest(const FString&)
{
    FPersonalExitFixture F;auto* M=F.NPC->GetExitKnowledge();
    F.NPC->SetActorLocation(FVector(1500,100,90));
    M->ObserveExits(FYUFSHazardSnapshot(),true);TestEqual(TEXT("Missing smoke coverage during fire does not certify new exit sight"),M->GetKnownExitCount(),0);
    TestTrue(TEXT("Explicit valid guidance can supply location independently"),M->RememberGuidedExit(F.Exit->GetActorLocation(),TEXT("Staff")));
    F.NPC->ApplyDistributedSpawnLocation(FVector(100,100,90));
    TestEqual(TEXT("Setup relocation resets knowledge learned at the old placement"),M->GetKnownExitCount(),0);
    return true;
}
#endif
