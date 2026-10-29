#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "NPC/Navigation/YUFSLocalMovementComponent.h"
#include "NPC/Perception/YUFSNPCPerceptionComponent.h"

struct FYUFSCrawlPostureTestAccess
{
    static bool Sync(AYUFSEvacuationNPC* NPC) { return NPC->UpdatePhysicalPosture(); }
};
namespace
{
UWorld* PostureWorld()
{
    const auto Settings=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    return UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Settings);
}
AActor* PostureBox(UWorld* World,FVector Center,FVector Extent)
{
    auto* Actor=World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Actor);
    Actor->SetRootComponent(Box); Box->SetBoxExtent(Extent); Box->SetCollisionProfileName(TEXT("BlockAll"));
    Box->RegisterComponent(); Actor->SetActorLocation(Center); return Actor;
}
FVector Feet(const AYUFSEvacuationNPC* NPC)
{
    return NPC->GetActorLocation()-FVector(0,0,NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
}
AYUFSEvacuationNPC* PostureNPC(UWorld* World,FVector Floor=FVector::ZeroVector)
{
    auto* NPC=World->SpawnActor<AYUFSEvacuationNPC>();
    NPC->SetActorLocation(Floor+FVector(0,0,NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.f));
    NPC->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    NPC->GetBehaviorStateMachine()->Config=NewObject<UYUFSBehaviorConfig>(NPC);
    NPC->GetNPCPerceptionComponent()->Config=NewObject<UYUFSPerceptionConfig>(NPC);
    return NPC;
}
void EnterCrawl(AYUFSEvacuationNPC* NPC)
{
    FYUFSNPCObservation Obs; Obs.SmokeDensityAtSelf=1.f;
    NPC->GetBehaviorStateMachine()->TickStateMachine(26.f,Obs);
    FYUFSCrawlPostureTestAccess::Sync(NPC);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSCrawlGeometryTest,"YUFS.NPC.Posture.CapsuleFeetMeshAndSafeStand",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSCrawlGeometryTest::RunTest(const FString&)
{
    auto* World=PostureWorld(); PostureBox(World,FVector(0,0,-10),FVector(400,400,10));
    auto* NPC=PostureNPC(World); const FVector InitialFeet=Feet(NPC);
    const FVector InitialMesh=NPC->GetMesh()->GetComponentLocation();
    const float InitialHalf=NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    EnterCrawl(NPC);
    NPC->GetMesh()->UpdateComponentToWorld(); // Engine crouch changes the mesh's relative location directly until the next component update.
    TestTrue(TEXT("Exposure changes actual collision posture"),NPC->IsCrouched());
    TestEqual(TEXT("Low half-height is 44 cm"),NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),44.f);
    TestTrue(TEXT("Lowering preserves the floor contact position"),Feet(NPC).Equals(InitialFeet,0.01));
    TestTrue(TEXT("Engine mesh compensation preserves the animation root world height"),NPC->GetMesh()->GetComponentLocation().Equals(InitialMesh,0.01));
    TestTrue(TEXT("Breathing point is measured from feet"),FMath::IsNearlyEqual((NPC->GetPawnViewLocation()-Feet(NPC)).Z,55.,0.01));
    TestTrue(TEXT("Crouched movement is limited to crawl speed"),NPC->GetCharacterMovement()->MaxWalkSpeedCrouched<=80.f);
    auto* Ceiling=PostureBox(World,FVector(0,0,140),FVector(120,120,40)); // bottom 100: low body fits, standing does not
    NPC->GetBehaviorStateMachine()->TickStateMachine(240.f,FYUFSNPCObservation());
    TestFalse(TEXT("A blocked stand-up does not report a posture change"),FYUFSCrawlPostureTestAccess::Sync(NPC));
    TestTrue(TEXT("The ceiling keeps actual collision low after exposure recovers"),NPC->IsCrouched());
    TestEqual(TEXT("Path samples stay low while stand-up is refused"),NPC->GetNavigator()->GetHazardSettings().SampleHeightCm,55.f);
    TestTrue(TEXT("A refused stand does not push the feet into the floor"),Feet(NPC).Equals(InitialFeet,0.01));
    Ceiling->Destroy();
    TestTrue(TEXT("Standing succeeds after headroom is available"),FYUFSCrawlPostureTestAccess::Sync(NPC));
    NPC->GetMesh()->UpdateComponentToWorld();
    TestFalse(TEXT("Standing clears the physical crouch flag"),NPC->IsCrouched());
    TestEqual(TEXT("Standing restores the class capsule"),NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),InitialHalf);
    TestTrue(TEXT("Standing preserves feet within engine sweep tolerance"),Feet(NPC).Equals(InitialFeet,0.1));
    TestTrue(TEXT("Standing restores the mesh root"),NPC->GetMesh()->GetComponentLocation().Equals(InitialMesh,0.1));
    World->DestroyWorld(false); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSCrawlLayerTest,"YUFS.NPC.Posture.LayeredSmokeBreathingPathAndGroundHeat",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSCrawlLayerTest::RunTest(const FString&)
{
    auto* World=PostureWorld(); auto* NPC=PostureNPC(World,FVector(40,0,0)); auto* Sense=NPC->GetNPCPerceptionComponent();
    auto Data=MakeShared<FYUFSHazardGrid,ESPMode::ThreadSafe>(); Data->Dimensions=FIntVector(16,16,12);
    Data->Density.Init(0,16*16*12); Data->Temperature.Init(0,16*16*12);
    for (int32 X=6;X<16;++X) for(int32 Y=0;Y<16;++Y) for(int32 Z=5;Z<12;++Z) Data->Density[(X*16+Y)*12+Z]=255;
    FYUFSHazardSnapshot Raw; Raw.Grid=Data; Raw.GridToWorld=FTransform(FQuat::Identity,FVector(-100,-100,0),FVector(20)); Raw.Status=EYUFSHazardDataStatus::Ready; Raw.Frame=0;
    Sense->UpdateFromSnapshot(Raw,0.f);
    TestEqual(TEXT("Standing breath samples the upper smoke layer"),Sense->GetSmokeDensity(),1.f);
    const TArray<FVector> Route{FVector(-60,0,2),FVector(140,0,2)};
    TestTrue(TEXT("Standing route refuses entering that layer"),Raw.ScorePath(Route,NPC->GetNavigator()->GetHazardSettings()).bUnsafeAhead);
    EnterCrawl(NPC); Sense->UpdateFromSnapshot(Raw,1.f);
    TestEqual(TEXT("Crawl breath reads the lower clear cell"),Sense->GetSmokeDensity(),0.f);
    TestTrue(TEXT("Looking upward still detects smoke; lowering is not blindness"),Sense->GetSmokeAboveNormalized()>0.f);
    TestFalse(TEXT("A clear lower layer permits the route after lowering"),Raw.ScorePath(Route,NPC->GetNavigator()->GetHazardSettings()).bUnsafeAhead);
    FYUFSNPCObservation Low; Low.SmokeDensityAtSelf=Sense->GetSmokeDensity();
    const float Before=NPC->GetBehaviorStateMachine()->GetSmokeExposure();
    NPC->GetBehaviorStateMachine()->TickStateMachine(40.f,Low);
    TestTrue(TEXT("Fresh lower air stops dose growth instead of using upper-layer smoke"),NPC->GetBehaviorStateMachine()->GetSmokeExposure()<Before);
    TestTrue(TEXT("Hysteresis avoids immediately standing back into the smoke"),NPC->GetBehaviorStateMachine()->IsCrawling());
    for (int32 X=6;X<16;++X) for(int32 Y=0;Y<16;++Y) Data->Temperature[(X*16+Y)*12+1]=255;
    TestTrue(TEXT("Ground heat remains unsafe despite lower clear air"),Raw.ScorePath(Route,NPC->GetNavigator()->GetHazardSettings()).bUnsafeAhead);
    Sense->UpdateFromSnapshot(Raw,2.f); TestTrue(TEXT("Ground heat still reaches nearby heat perception"),Sense->GetNearbyHeat()>0.f);
    for (auto& Value:Data->Temperature) Value=0;
    for (int32 X=6;X<16;++X) for(int32 Y=0;Y<16;++Y) Data->Temperature[(X*16+Y)*12+4]=255;
    TestTrue(TEXT("Heat at the top of the collision body is not hidden by a lower breathing point"),Raw.ScorePath(Route,NPC->GetNavigator()->GetHazardSettings()).bUnsafeAhead);
    for (auto& Value:Data->Temperature) Value=0;
    for (int32 X=6;X<16;++X) for(int32 Y=0;Y<16;++Y) Data->Temperature[(X*16+Y)*12+3]=255;
    TestTrue(TEXT("Heat between the breathing point and body top is also sampled"),Raw.ScorePath(Route,NPC->GetNavigator()->GetHazardSettings()).bUnsafeAhead);
    World->DestroyWorld(false); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSCrawlClearanceTest,"YUFS.NPC.Posture.LowClearanceAndRadiusClamp",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSCrawlClearanceTest::RunTest(const FString&)
{
    auto* World=PostureWorld(); PostureBox(World,FVector(0,0,-10),FVector(400,150,10));
    PostureBox(World,FVector(130,0,150),FVector(30,150,50));
    auto* NPC=PostureNPC(World); auto* Traffic=NPC->GetLocalMovement();
    TestFalse(TEXT("A standing capsule cannot pass a 100 cm clearance"),Traffic->IsPhysicalCorridorClear(FVector(260,0,0)));
    NPC->GetBehaviorStateMachine()->Config->CrawlCapsuleHalfHeightCm=1.f;
    EnterCrawl(NPC);
    TestEqual(TEXT("Invalid low setting cannot shrink below the body radius"),NPC->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(),NPC->GetCapsuleComponent()->GetUnscaledCapsuleRadius());
    TestTrue(TEXT("Physical recovery validates the smaller real capsule beneath the obstacle"),Traffic->IsPhysicalCorridorClear(FVector(260,0,0)));
    TestFalse(TEXT("Lower posture still cannot cross unsupported floor"),Traffic->IsPhysicalCorridorClear(FVector(0,400,0)));
    World->DestroyWorld(false); return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSCrawlReplayTest,"YUFS.NPC.Posture.ReplayRestoresCollisionAndBreathing",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSCrawlReplayTest::RunTest(const FString&)
{
    auto* World=PostureWorld(); auto* NPC=PostureNPC(World);
    const auto Standing=NPC->BuildTimelineSnapshot(); EnterCrawl(NPC); const auto Low=NPC->BuildTimelineSnapshot();
    NPC->SetTimelinePlaybackMode(true); NPC->ApplyTimelineSnapshot(Standing);
    TestFalse(TEXT("Seeking to standing does not retain the last low capsule"),NPC->IsCrouched());
    TestTrue(TEXT("Standing snapshot restores its recorded centre"),NPC->GetActorLocation().Equals(Standing.Location));
    NPC->ApplyTimelineSnapshot(Low);
    TestTrue(TEXT("Seeking back restores physical low posture"),NPC->IsCrouched());
    TestEqual(TEXT("Replay restores recorded capsule dimensions"),NPC->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(),Low.CapsuleHalfHeightCm);
    TestTrue(TEXT("Replay lower breathing point stays above the recorded feet"),FMath::IsNearlyEqual((NPC->GetPawnViewLocation()-Feet(NPC)).Z,55.,0.01));
    World->DestroyWorld(false); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSCrawlPauseLifecycleTest,"YUFS.NPC.Posture.PausePreservesLowCollisionAndMovementMode",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSCrawlPauseLifecycleTest::RunTest(const FString&)
{
    auto* World=PostureWorld(); auto* NPC=PostureNPC(World); EnterCrawl(NPC);
    const FVector InitialFeet=Feet(NPC);
    NPC->SetLiveSimulationPaused(true);
    TestEqual(TEXT("Pause preserves walking mode instead of forcing an uncrouch-capable NONE mode"),NPC->GetCharacterMovement()->MovementMode.GetValue(),MOVE_Walking);
    TestFalse(TEXT("Paused low character cannot execute component movement ticks"),NPC->GetCharacterMovement()->IsComponentTickEnabled());
    TestTrue(TEXT("Pause keeps actual lowered posture"),NPC->IsCrouched());
    NPC->SetLiveSimulationPaused(false);
    TestTrue(TEXT("Resume retains lowered posture"),NPC->IsCrouched());
    TestEqual(TEXT("Resume keeps the actual low collision shape"),NPC->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(),44.f);
    TestTrue(TEXT("Pause/resume does not move the feet"),Feet(NPC).Equals(InitialFeet,.01));
    TestTrue(TEXT("Crouched speed remains limited after resume"),NPC->GetCharacterMovement()->MaxWalkSpeedCrouched<=80.f);
    World->DestroyWorld(false); return true;
}
#endif
