#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Animation/YUFSActionAnimationComponent.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "NPC/Integration/YUFSNpcSuppressionComponent.h"
#include "Fire/YUFSFireExtinguisher.h"
#include "Simulation/YUFSAuthoredSuppressionScenario.h"
#include "Simulation/YUFSSimulationController.h"
#include "Simulation/YUFSGameInstance.h"
#include "Kismet/GameplayStatics.h"

namespace
{
struct FPresentationFixture
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    AYUFSEvacuationNPC* Npc = nullptr;
    AYUFSFireExtinguisher* Tool = nullptr;
    UYUFSNpcSuppressionComponent* Presentation = nullptr;
    FPresentationFixture()
    {
        FActorSpawnParameters Spawn;
        Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Npc = World->SpawnActor<AYUFSEvacuationNPC>(FVector(0,0,90), FRotator::ZeroRotator, Spawn);
        Tool = World->SpawnActor<AYUFSFireExtinguisher>(FVector(60,0,3), FRotator::ZeroRotator, Spawn);
        Npc->GetMesh()->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/NPCs/Crawling__1_.Crawling__1_")));
        Npc->GetActionAnimationComponent()->Initialize(Npc->GetMesh(), 37);
        Presentation = Npc->GetSuppressionComponent();
    }
    ~FPresentationFixture() { Presentation->Cancel(false); World->DestroyWorld(false); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSuppressionPresentationLifecycleTest,
    "YUFS.NPC.Suppression.Presentation.ExplicitGestureDoesNotForgeFireKnowledge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSuppressionPresentationLifecycleTest::RunTest(const FString&)
{
    FPresentationFixture F;
    TestFalse(TEXT("No automatic presentation on spawn"), F.Presentation->IsVisualPresentationActive());
    const FTransform OriginalTransform = F.Tool->GetActorTransform();
    TestTrue(TEXT("Explicit presentation starts without FDS data"), F.Presentation->StartVisualPresentation(F.Tool, FVector(300,0,65), 2.f));
    TestFalse(TEXT("Preview excludes learning transitions"), F.Npc->bLogTransitions);
    F.Presentation->TickVisualPresentation(1.6f);
    TestEqual(TEXT("Real extinguisher enters spraying state"), F.Tool->GetExtinguisherState(), EYUFSFireExtinguisherState::Spraying);
    TestTrue(TEXT("Presentation is visibly spraying"), F.Presentation->IsSpraying());
    TArray<UStaticMeshComponent*> Meshes;
    F.Tool->GetComponents(Meshes);
    bool bVisibleSpray = false;
    for (const auto* Mesh : Meshes)
        if (Mesh->GetFName() == TEXT("SprayStream")) bVisibleSpray = Mesh->IsVisible();
    TestTrue(TEXT("Nozzle spray mesh is visible"), bVisibleSpray);
    TestFalse(TEXT("Ordinary suppression executor remains inactive"), F.Presentation->IsActive());
    TestFalse(TEXT("Preview does not make optional interactions safe"), F.Npc->AllowsOptionalInteractions());
    TestEqual(TEXT("JJW behavior is unchanged"), F.Npc->GetBehaviorStateMachine()->GetCurrentState(), EYUFSBehaviorState::Normal);
    TestEqual(TEXT("Policy action is unchanged"), F.Npc->GetLastAction(), EYUFSAction::Idle);
    TestEqual(TEXT("Gesture does not consume simulated agent"), F.Tool->GetRemainingAgentNormalized(), 1.f);
    F.Presentation->TickVisualPresentation(.5f);
    TestFalse(TEXT("Bounded presentation finishes"), F.Presentation->IsVisualPresentationActive());
    TestEqual(TEXT("Prop becomes available again"), F.Tool->GetExtinguisherState(), EYUFSFireExtinguisherState::Available);
    TestTrue(TEXT("Prop position, rotation and scale are restored"), F.Tool->GetActorTransform().Equals(OriginalTransform));
    TestTrue(TEXT("Learning logging is restored after dropping the preview transition"), F.Npc->bLogTransitions);
    TestEqual(TEXT("Outcome is presentation complete, not fire extinguished"), F.Presentation->GetLastStopReason(), FName(TEXT("PresentationComplete")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSuppressionAuthoredApproachTest,
    "YUFS.NPC.Suppression.Authored.PickupThenApproachBeforeSpraying",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSuppressionAuthoredApproachTest::RunTest(const FString&)
{
    FPresentationFixture F;
    const FVector SprayFeet(200,0,0);
    TestTrue(TEXT("Authored scenario reserves existing tool"), F.Presentation->StartAuthoredApproach(F.Tool, FVector(450,0,65), SprayFeet, 2.f));
    TestFalse(TEXT("Pickup requests physical travel to spray position"), F.Presentation->TickVisualPresentation(2.f));
    TestEqual(TEXT("Tool is held, not spraying while walking"), F.Tool->GetExtinguisherState(), EYUFSFireExtinguisherState::Held);
    TestTrue(TEXT("Movement uses the validated spray point"), F.Presentation->GetMovementTarget().Equals(SprayFeet));
    F.Npc->SetActorLocation(SprayFeet + FVector(0,0,90));
    F.Presentation->TickVisualPresentation(1.6f);
    TestEqual(TEXT("Spray starts only after approach and preparation"), F.Tool->GetExtinguisherState(), EYUFSFireExtinguisherState::Spraying);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSuppressionPresentationCancelTest,
    "YUFS.NPC.Suppression.Presentation.ReplayCancelsAndReleasesProp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSuppressionPresentationCancelTest::RunTest(const FString&)
{
    FPresentationFixture F;
    TestFalse(TEXT("Invalid duration is rejected"), F.Presentation->StartVisualPresentation(F.Tool, FVector(300,0,65), 0.f));
    TestTrue(TEXT("Presentation starts"), F.Presentation->StartVisualPresentation(F.Tool, FVector(300,0,65), 10.f));
    F.Presentation->TickVisualPresentation(1.6f);
    F.Npc->SetTimelinePlaybackMode(true);
    TestFalse(TEXT("Timeline mode cancels the live gesture"), F.Presentation->IsVisualPresentationActive());
    TestFalse(TEXT("Spray cannot leak into replay"), F.Presentation->IsSpraying());
    TestNull(TEXT("Reservation is released"), F.Tool->GetOwnerActor());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSuppressionAuthoredSafetyTest,
    "YUFS.NPC.Suppression.Authored.EmergencyAndExternalOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSuppressionAuthoredSafetyTest::RunTest(const FString&)
{
    FPresentationFixture F;
    TestTrue(TEXT("Compatible idle NPC can perform an authored gesture"), F.Presentation->CanStartAuthoredGesture());
    F.Npc->bUseExternalMotionDriver = true;
    TestFalse(TEXT("External motion ownership is preserved"), F.Presentation->CanStartAuthoredGesture());
    F.Npc->bUseExternalMotionDriver = false;
    F.Presentation->bEnabled = false;
    TestFalse(TEXT("Disabled executor cannot be started explicitly"), F.Presentation->StartVisualPresentation(F.Tool, FVector(300,0,65), 2.f));
    F.Presentation->bEnabled = true;
    FYUFSNPCObservation Observation;
    Observation.NearbyHeatNormalized = 1.f;
    TestTrue(TEXT("Immediate heat interrupts an authored gesture"), F.Presentation->ShouldInterruptAuthoredGesture(Observation));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSuppressionAuthoredLifecycleTest,
    "YUFS.NPC.Suppression.Authored.WaitingAndPausedDoNotStart",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSuppressionAuthoredLifecycleTest::RunTest(const FString&)
{
    FPresentationFixture F;
    auto* Controller = F.World->SpawnActor<AYUFSSimulationController>();
    auto* Scenario = F.World->SpawnActor<AYUFSAuthoredSuppressionScenario>();
    Scenario->bEnabled = true;
    Scenario->MapName = UGameplayStatics::GetCurrentLevelName(Scenario, true);
    Scenario->TargetWorldLocation = FVector(300,0,65);
    Scenario->Tick(10.f);
    TestFalse(TEXT("Play alone does not force a gesture before Start"), Scenario->HasStarted());
    Controller->StartSimulation();
    Scenario->Tick(10.f);
    TestFalse(TEXT("Countdown does not start a gesture"), Scenario->HasStarted());
    Controller->PauseSimulation();
    Scenario->Tick(10.f);
    TestFalse(TEXT("Pause does not start a gesture"), Scenario->HasStarted());
    TestTrue(TEXT("Ordinary project settings enable prop setup"), GetDefault<UYUFSGameInstance>()->bEnableBuildingInteractions);
    return true;
}
#endif
