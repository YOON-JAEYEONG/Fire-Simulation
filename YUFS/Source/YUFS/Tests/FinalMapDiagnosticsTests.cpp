#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Debug/YUFSMapDiagnostics.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

namespace
{
AActor* Box(UWorld* World, FVector Center, FVector Extent)
{
    auto* Actor = World->SpawnActor<AActor>();
    auto* Component = NewObject<UBoxComponent>(Actor);
    Actor->SetRootComponent(Component);
    Component->SetBoxExtent(Extent);
    Component->SetCollisionProfileName(TEXT("BlockAll"));
    Component->RegisterComponent();
    Actor->SetActorLocation(Center);
    return Actor;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSFinalMapPhysicalProbeTest,
    "YUFS.NPC.MapIntegration.PhysicalFloorBodyAndReadOnlyProbe",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSFinalMapPhysicalProbeTest::RunTest(const FString&)
{
    const auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false)
        .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto* World = UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Settings);
    auto* Floor = Box(World,FVector(0,0,-10),FVector(500,500,10));
    const FTransform Original = Floor->GetActorTransform();
    const auto Clear = UYUFSMapDiagnostics::ProbeLocation(World,FVector::ZeroVector,ACharacter::StaticClass());
    TestTrue(TEXT("A real floor supports the candidate"),Clear.bSupported);
    TestTrue(TEXT("A full standing body fits above the floor"),Clear.bBodyClear);
    TestFalse(TEXT("Physical floor is not invented navigation data"),Clear.bOnNavigation);
    TestEqual(TEXT("Report physical floor, not a projected or elevated approximation"),Clear.FloorLocation.Z,0.0);
    auto* Ceiling = Box(World,FVector(0,0,140),FVector(100,100,10));
    const auto Blocked = UYUFSMapDiagnostics::ProbeLocation(World,FVector::ZeroVector,ACharacter::StaticClass());
    TestTrue(TEXT("Ceiling does not remove the supporting floor"),Blocked.bSupported);
    TestFalse(TEXT("Full capsule detects the low ceiling"),Blocked.bBodyClear);
    TestFalse(TEXT("A missing floor cannot become a valid placement"),
        UYUFSMapDiagnostics::ProbeLocation(World,FVector(1500,0,0),ACharacter::StaticClass()).bSupported);
    TestTrue(TEXT("Diagnostic never moves the floor"),Original.Equals(Floor->GetActorTransform()));
    TestTrue(TEXT("Diagnostic never removes obstacle collision"),Ceiling->GetActorEnableCollision());
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSFinalMapFloorIsolationTest,
    "YUFS.NPC.MapIntegration.ProbeKeepsSeparateFloorsAndMissingRoutes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSFinalMapFloorIsolationTest::RunTest(const FString&)
{
    const auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false)
        .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    auto* World = UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Settings);
    Box(World,FVector(0,0,-10),FVector(500,500,10));
    Box(World,FVector(0,0,340),FVector(500,500,10));
    const auto Lower = UYUFSMapDiagnostics::ProbeLocation(World,FVector::ZeroVector,ACharacter::StaticClass());
    const auto Upper = UYUFSMapDiagnostics::ProbeLocation(World,FVector(0,0,350),ACharacter::StaticClass());
    TestEqual(TEXT("Lower floor query cannot snap to the floor above"),Lower.FloorLocation.Z,0.0);
    TestEqual(TEXT("Upper floor query cannot fall through to the floor below"),Upper.FloorLocation.Z,350.0);
    TestFalse(TEXT("No navmesh cannot produce a complete route"),
        UYUFSMapDiagnostics::ProbeRoute(World,FVector::ZeroVector,FVector(200,0,350)).bComplete);
    TestFalse(TEXT("Missing character template is explicitly invalid"),
        UYUFSMapDiagnostics::ProbeLocation(World,FVector::ZeroVector,nullptr).bSupported);
    World->DestroyWorld(false);
    return true;
}
#endif
