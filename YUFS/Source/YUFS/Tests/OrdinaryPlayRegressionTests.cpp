#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Navigation/YUFSLocalMovementComponent.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"

struct FYUFSTrafficRegressionAccess
{
    static void Wait(AYUFSEvacuationNPC* A, AYUFSEvacuationNPC* B)
    {
        auto* T = A->GetLocalMovement();
        T->YieldingTo = B;
        T->State = EYUFSLocalMovementState::Yielding;
    }
    static void Exhaust(AYUFSEvacuationNPC* A)
    {
        auto* T=A->GetLocalMovement(); T->Attempts=T->MaxRecoveryAttempts; T->Cooldown=0.f;
    }
    static float Cooldown(AYUFSEvacuationNPC* A) { return A->GetLocalMovement()->Cooldown; }
    static int32 Attempts(AYUFSEvacuationNPC* A) { return A->GetLocalMovement()->Attempts; }
    static int32 Select(AYUFSEvacuationNPC* A,const TArray<FVector>& Candidates,FVector Direction)
    { return A->GetLocalMovement()->SelectRecoverySpace(Candidates,Direction); }
    static void HoldInteraction(AYUFSEvacuationNPC* A,bool bHold) { A->bInteractionHoldingPosition=bHold; }
    static void Route(AYUFSEvacuationNPC* A,FVector Goal)
    {
        auto* Nav=A->GetNavigator();Nav->NavigationStatus=EYUFSNavigationStatus::Moving;
        Nav->CurrentPath={A->GetActorLocation()-FVector(0,0,88),Goal};Nav->CurrentWaypointIndex=1;
    }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSStationaryPeerRecoveryRegression,
    "YUFS.NPC.Navigation.Traffic.StationaryInteractionPeerCanBePassedSafely",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSStationaryPeerRecoveryRegression::RunTest(const FString&)
{
    const auto Settings=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Settings);
    auto* Floor=World->SpawnActor<AActor>();auto* Shape=NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(Shape);Shape->SetBoxExtent(FVector(500,500,10));Shape->SetCollisionProfileName(TEXT("BlockAll"));Shape->RegisterComponent();Floor->SetActorLocation(FVector(0,0,-10));
    auto* NPC=World->SpawnActor<AYUFSEvacuationNPC>();auto* Operator=World->SpawnActor<AYUFSEvacuationNPC>();
    const float Half=NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    NPC->SetActorLocation(FVector(0,0,Half+2));Operator->SetActorLocation(FVector(0,100,Half+2));
    const TArray<FVector> Spaces={FVector(0,-90,0),FVector(120,0,0)};
    FYUFSTrafficRegressionAccess::HoldInteraction(Operator,true);
    FYUFSTrafficRegressionAccess::Wait(NPC,Operator);
    TestTrue(TEXT("The physical person prevents direct passage"),!NPC->GetLocalMovement()->IsPhysicalCorridorClear(FVector(0,200,0)));
    TestTrue(TEXT("An actually supported side space is available"),NPC->GetLocalMovement()->IsPhysicalCorridorClear(Spaces[1]));
    TestEqual(TEXT("After yielding, prefer a safe way around the stationary operator rather than repeated backtracking"),
        FYUFSTrafficRegressionAccess::Select(NPC,Spaces,FVector(0,1,0)),1);
    Operator->SetActorLocation(FVector(0,300,Half+2));
    const FVector Approach=NPC->GetLocalMovement()->ResolveDirection(FVector(0,1,0),.1f,0);
    TestTrue(TEXT("A distant stationary body allows a reduced approach to reach the passing space"),Approach.Y>0.f && Approach.Y<1.f);
    TestFalse(TEXT("That reduced approach is not a stationary yield reservation"),NPC->GetLocalMovement()->IsDeliberatelyWaiting());
    Operator->SetActorLocation(FVector(0,100,Half+2));
    TestTrue(TEXT("At body-clearance distance the person is still yielded to, not pushed through"),NPC->GetLocalMovement()->ResolveDirection(FVector(0,1,0),.1f,0).IsNearlyZero());
    Operator->SetActorLocation(FVector(0,300,Half+2));
    FYUFSTrafficRegressionAccess::Wait(NPC,Operator);
    TestEqual(TEXT("Predictive yielding at 300cm must look beyond the actual blocker, not just the next 180cm"),
        FYUFSTrafficRegressionAccess::Select(NPC,Spaces,FVector(0,1,0)),1);
    const TArray<FVector> PassingSpaces={Spaces[0],Spaces[1],FVector(120,80,0)};
    TestTrue(TEXT("The forward diagonal is physically reachable without crossing the person"),NPC->GetLocalMovement()->IsPhysicalCorridorClear(PassingSpaces[2]));
    TestEqual(TEXT("A passing recovery should advance around the body rather than immediately return to the same centre line"),
        FYUFSTrafficRegressionAccess::Select(NPC,PassingSpaces,FVector(0,1,0)),2);
    TestTrue(TEXT("Selection never disables the operator collision"),Operator->GetActorEnableCollision());
    FYUFSTrafficRegressionAccess::HoldInteraction(Operator,false);
    FYUFSTrafficRegressionAccess::Route(Operator,FVector(0,-500,0));Operator->GetCharacterMovement()->Velocity=FVector(0,-100,0);
    FYUFSTrafficRegressionAccess::Wait(Operator,NPC);
    TestEqual(TEXT("An opposing queue can make space sideways instead of only retreating into its own followers"),
        FYUFSTrafficRegressionAccess::Select(NPC,Spaces,FVector(0,1,0)),1);
    FYUFSTrafficRegressionAccess::Route(Operator,FVector(0,900,0));Operator->GetCharacterMovement()->Velocity=FVector(0,100,0);
    FYUFSTrafficRegressionAccess::Wait(Operator,NPC);
    TestEqual(TEXT("A temporarily yielding queue member keeps make-room priority"),
        FYUFSTrafficRegressionAccess::Select(NPC,Spaces,FVector(0,1,0)),0);
    World->DestroyWorld(false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSStaticObstacleRecoveryRegression,
    "YUFS.NPC.Navigation.Traffic.FixedObstacleRecoveryChecksContinuation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSStaticObstacleRecoveryRegression::RunTest(const FString&)
{
    const auto Settings=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Settings);
    auto Box=[&](FVector Center,FVector Extent)
    {
        auto* Actor=World->SpawnActor<AActor>();auto* Shape=NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Shape);Shape->SetBoxExtent(Extent);Shape->SetCollisionProfileName(TEXT("BlockAll"));
        Shape->RegisterComponent();Actor->SetActorLocation(Center);return Actor;
    };
    Box(FVector(0,0,-10),FVector(450,450,10));
    Box(FVector(0,80,60),FVector(18,30,60));
    auto* NPC=World->SpawnActor<AYUFSEvacuationNPC>();
    NPC->SetActorLocation(FVector(0,0,NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2));
    const TArray<FVector> Candidates={FVector(0,-90,0),FVector(90,0,0)};
    TestTrue(TEXT("Backing away itself is physically possible"),NPC->GetLocalMovement()->IsPhysicalCorridorClear(Candidates[0]));
    TestTrue(TEXT("Lateral recovery itself is physically possible"),NPC->GetLocalMovement()->IsPhysicalCorridorClear(Candidates[1]));
    const FVector Initial=NPC->GetActorLocation();
    TestEqual(TEXT("Choose the side with onward space instead of repeating the same backwards loop"),
        FYUFSTrafficRegressionAccess::Select(NPC,Candidates,FVector(0,1,0)),1);
    TestTrue(TEXT("Looking ahead does not relocate the actor"),NPC->GetActorLocation().Equals(Initial));
    auto* Peer=World->SpawnActor<AYUFSEvacuationNPC>();Peer->SetActorLocation(FVector(350,350,90));
    FYUFSTrafficRegressionAccess::Wait(Peer,NPC); // A temporary queue member, not an indefinitely stationary operator.
    FYUFSTrafficRegressionAccess::Wait(NPC,Peer);
    TestEqual(TEXT("Yielding still prioritizes making room for the reserved person"),
        FYUFSTrafficRegressionAccess::Select(NPC,Candidates,FVector(0,1,0)),0);
    TestEqual(TEXT("No valid candidate cannot invent a recovery destination"),
        FYUFSTrafficRegressionAccess::Select(NPC,{},FVector(0,1,0)),INDEX_NONE);
    World->DestroyWorld(false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSTrafficExhaustionRegression,
    "YUFS.NPC.Navigation.Traffic.ExhaustedRecoveryRechecksWithBackoff",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSTrafficExhaustionRegression::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    auto* NPC=World->SpawnActor<AYUFSEvacuationNPC>();
    FYUFSTrafficRegressionAccess::Exhaust(NPC);
    TestFalse(TEXT("Unavailable navigation cannot invent a recovery space"),NPC->GetLocalMovement()->TryRecovery(FVector(1,0,0),0));
    TestTrue(TEXT("An exhausted batch schedules a future space check instead of becoming permanently frozen"),
        FYUFSTrafficRegressionAccess::Cooldown(NPC)>=NPC->GetLocalMovement()->BlockedRecheckSeconds);
    TestFalse(TEXT("A second attempt cannot bypass backoff"),NPC->GetLocalMovement()->TryRecovery(FVector(1,0,0),0));
    TestEqual(TEXT("Backoff does not reset the spent budget without a valid candidate"),
        FYUFSTrafficRegressionAccess::Attempts(NPC),NPC->GetLocalMovement()->MaxRecoveryAttempts);
    TestFalse(TEXT("A paused traffic peer is not predicted as advancing through a doorway"),
        UYUFSLocalMovementComponent::IsMovingPeer(true,false,true));
    World->DestroyWorld(false); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSTrafficStairRecoveryRegression,
    "YUFS.NPC.Navigation.Traffic.ContinuousStairRecoveryAndVoidRejection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSTrafficStairRecoveryRegression::RunTest(const FString&)
{
    const auto Settings=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Settings);
    auto Box=[&](FVector Center,FVector Extent)
    {
        auto* Actor=World->SpawnActor<AActor>(); auto* Shape=NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Shape); Shape->SetBoxExtent(Extent); Shape->SetCollisionProfileName(TEXT("BlockAll"));
        Shape->RegisterComponent(); Actor->SetActorLocation(Center); return Actor;
    };
    Box(FVector(0,0,-10),FVector(350,150,10));
    for (int32 I=0;I<4;++I) Box(FVector(70+60*I,0,10*(I+1)),FVector(30,150,10*(I+1)));
    auto* NPC=World->SpawnActor<AYUFSEvacuationNPC>();
    const float Half=NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    NPC->SetActorLocation(FVector(0,0,Half+2));
    auto* Traffic=NPC->GetLocalMovement();
    const FVector Initial=NPC->GetActorLocation();
    TestTrue(TEXT("Several walkable steps can exceed one MaxStepHeight in total"),Traffic->IsPhysicalCorridorClear(FVector(250,0,80)));
    TestTrue(TEXT("Validation never moves the actor"),NPC->GetActorLocation().Equals(Initial));
    TestFalse(TEXT("Same XY on an upper storey cannot be a shortcut"),Traffic->IsPhysicalCorridorClear(FVector(0,0,350)));
    auto* Wall=Box(FVector(140,0,150),FVector(10,150,150));
    TestFalse(TEXT("A real wall still blocks the recovery corridor"),Traffic->IsPhysicalCorridorClear(FVector(250,0,80)));
    Wall->Destroy();
    TestFalse(TEXT("No floor support cannot be repaired by a capsule-only sweep"),Traffic->IsPhysicalCorridorClear(FVector(0,400,0)));
    World->DestroyWorld(false); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSTrafficCycleRegression,
    "YUFS.NPC.Navigation.Traffic.ThreeAgentCycleHasOneLeader",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSTrafficCycleRegression::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    TArray<AYUFSEvacuationNPC*> Actors;
    for (int32 I=0; I<3; ++I) Actors.Add(World->SpawnActor<AYUFSEvacuationNPC>());
    Actors.Sort([](const AYUFSEvacuationNPC& A, const AYUFSEvacuationNPC& B) { return A.GetUniqueID()<B.GetUniqueID(); });
    for (int32 I=0; I<3; ++I) FYUFSTrafficRegressionAccess::Wait(Actors[I], Actors[(I+1)%3]);
    TestTrue(TEXT("One cycle leader proceeds"), Actors[0]->GetLocalMovement()->IsCycleLeader(Actors[1]));
    TestFalse(TEXT("Second resident yields"), Actors[1]->GetLocalMovement()->IsCycleLeader(Actors[2]));
    TestFalse(TEXT("Third resident yields"), Actors[2]->GetLocalMovement()->IsCycleLeader(Actors[0]));
    Actors[2]->GetLocalMovement()->Reset();
    TestFalse(TEXT("An ordinary queue is not a cycle"), Actors[0]->GetLocalMovement()->IsCycleLeader(Actors[1]));
    World->DestroyWorld(false);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSResolvedAvoidanceRegression,
    "YUFS.NPC.Navigation.Traffic.EvacuatedResidentStopsAvoidance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSResolvedAvoidanceRegression::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    auto* Npc = World->SpawnActor<AYUFSEvacuationNPC>();
    Npc->GetCharacterMovement()->Velocity = FVector(100,0,0);
    Npc->NotifyEpisodeFinished(EYUFSTerminalReason::ReachedExit);
    TestFalse(TEXT("Resolved NPC cannot remain an RVO obstacle"), Npc->GetCharacterMovement()->bUseRVOAvoidance);
    TestTrue(TEXT("Resolved NPC has no residual velocity"), Npc->GetVelocity().IsNearlyZero());
    TestEqual(TEXT("Component movement stops even before actor tick is disabled"), Npc->GetCharacterMovement()->MovementMode.GetValue(), MOVE_None);
    Npc->SetTimelinePlaybackMode(true);
    Npc->SetTimelinePlaybackMode(false);
    TestTrue(TEXT("Resuming live mode restores avoidance"), Npc->GetCharacterMovement()->bUseRVOAvoidance);
    World->DestroyWorld(false);
    return true;
}
#endif
