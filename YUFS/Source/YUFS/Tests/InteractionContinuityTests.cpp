#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Debug/YUFSInteractionPreview.h"
#include "Fire/YUFSInteractionDoor.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Decision/YUFSIntentComponent.h"
#include "NPC/Integration/YUFSNpcEnvironmentInteraction.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FYUFSInteractionContinuityTest,
    "YUFS.NPC.Interaction.ContinuousDoorHelpEvacuation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FYUFSInteractionContinuityTest::RunTest(const FString& Parameters)
{
    const auto Settings = UWorld::InitializationValues().AllowAudioPlayback(false)
        .CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr,
        true, ERHIFeatureLevel::Num, &Settings);
    if (!TestNotNull(TEXT("isolated continuity world created"), World)) return false;

    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* User = World->SpawnActor<AYUFSEvacuationNPC>(FVector(40, 10, 90), FRotator::ZeroRotator, Spawn);
    auto* Recipient = World->SpawnActor<AYUFSEvacuationNPC>(FVector(180, 100, 90), FRotator::ZeroRotator, Spawn);
    auto* Door = World->SpawnActor<AYUFSInteractionDoor>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
    auto* Preview = World->SpawnActor<AYUFSInteractionPreview>();
    if (!TestNotNull(TEXT("helper spawned"), User)
        || !TestNotNull(TEXT("recipient spawned"), Recipient)
        || !TestNotNull(TEXT("door spawned"), Door)
        || !TestNotNull(TEXT("fixture spawned"), Preview))
    {
        World->DestroyWorld(false);
        return false;
    }

    // No BeginPlay or navigation: initial placement is outside this test. Enter
    // the already-initialized door stage through the fixture's test friendship.
    Preview->User = User;
    Preview->Recipient = Recipient;
    Preview->Door = Door;
    Preview->bSimulationStarted = true;
    Preview->Stage = EYUFSInteractionPreviewStage::Door;
    Preview->Elapsed = 3.f;
    User->bInteractionPreviewControlled = true;
    Recipient->bInteractionPreviewControlled = true;
    TestTrue(TEXT("opening can begin"), Door->TryUse(User));
    Door->Tick(5.f);
    TestTrue(TEXT("door is open before the help transition"), Door->IsOpen());

    const FVector UserPosition = User->GetActorLocation();
    const FVector RecipientPosition = Recipient->GetActorLocation();
    const FTransform DoorTransform = Door->GetActorTransform();

    Preview->StartSequence();
    TestTrue(TEXT("repeat Start cannot recreate the door"), Preview->Door.Get() == Door);
    TestTrue(TEXT("repeat Start cannot replace the helper"), Preview->User.Get() == User);
    TestTrue(TEXT("repeat Start cannot replace the recipient"), Preview->Recipient.Get() == Recipient);
    TestTrue(TEXT("repeat Start retains stage"), Preview->Stage == EYUFSInteractionPreviewStage::Door);
    TestEqual(TEXT("repeat Start retains elapsed time"), Preview->Elapsed, 3.f);

    Preview->SetSimulationPaused(true);
    Preview->Tick(50.f);
    Preview->StartHelp();
    TestTrue(TEXT("paused stage cannot change or time out"), Preview->Stage == EYUFSInteractionPreviewStage::Door);
    TestEqual(TEXT("paused stage timer is frozen"), Preview->Elapsed, 3.f);
    TestFalse(TEXT("door actor is paused with the fixture"), Door->IsActorTickEnabled());
    Preview->SetSimulationPaused(false);

    Preview->StartHelp();
    TestTrue(TEXT("door stage advances to help"), Preview->Stage == EYUFSInteractionPreviewStage::Help);
    TestTrue(TEXT("helper stays at current position"), User->GetActorLocation().Equals(UserPosition));
    TestTrue(TEXT("recipient stays at current position"), Recipient->GetActorLocation().Equals(RecipientPosition));
    TestTrue(TEXT("same door remains valid"), IsValid(Door) && Preview->Door.Get() == Door);
    TestTrue(TEXT("door retains world transform"), Door->GetActorTransform().Equals(DoorTransform));
    TestTrue(TEXT("opened door stays open"), Door->IsOpen());
    TestTrue(TEXT("help target requests assistance"), Recipient->EnvironmentInteraction->bNeedsAssistance);
    TestTrue(TEXT("same helper now assists"), User->InteractionPreviewBehavior == EYUFSHighLevelBehavior::AssistOther);

    Preview->Elapsed = 1.f;
    Preview->StartHelp();
    TestEqual(TEXT("repeat help transition does not restart the stage"), Preview->Elapsed, 1.f);
    Preview->ReleaseResidentsToEvacuation();
    TestFalse(TEXT("helper is released to ordinary decisions"), User->bInteractionPreviewControlled);
    TestFalse(TEXT("recipient is released to ordinary decisions"), Recipient->bInteractionPreviewControlled);
    TestFalse(TEXT("completed handoff clears assistance need"), Recipient->EnvironmentInteraction->bNeedsAssistance);
    TestTrue(TEXT("helper handoff does not teleport"), User->GetActorLocation().Equals(UserPosition));
    TestTrue(TEXT("recipient handoff does not teleport"), Recipient->GetActorLocation().Equals(RecipientPosition));
    TestFalse(TEXT("helper is not hidden to fake success"), User->IsHidden());
    TestFalse(TEXT("recipient is not hidden to fake success"), Recipient->IsHidden());
    TestTrue(TEXT("no level or safe path yields helper shelter, not false evacuation"), User->GetCurrentIntent() == EYUFSIntent::Shelter);
    TestTrue(TEXT("no level or safe path yields recipient shelter, not false evacuation"), Recipient->GetCurrentIntent() == EYUFSIntent::Shelter);
    TestTrue(TEXT("handoff starts monitoring rather than claiming completion"), Preview->Stage == EYUFSInteractionPreviewStage::Evacuating);
    TestTrue(TEXT("door persists after both residents are released"), IsValid(Door) && Preview->Door.Get() == Door && Door->IsOpen());

    Preview->Elapsed = 7.f;
    Preview->MonitorTimer = 4.f;
    Preview->SetSimulationPaused(true);
    Preview->Tick(200.f);
    TestEqual(TEXT("paused evacuation timer is frozen"), Preview->Elapsed, 7.f);
    TestEqual(TEXT("paused monitoring timer is frozen"), Preview->MonitorTimer, 4.f);
    TestTrue(TEXT("pause cannot produce monitoring timeout"), Preview->Stage == EYUFSInteractionPreviewStage::Evacuating);
    Preview->SetSimulationPaused(false);
    Preview->StartSequence();
    TestTrue(TEXT("repeat Start after handoff cannot take control back"), !User->bInteractionPreviewControlled && !Recipient->bInteractionPreviewControlled);
    int32 DoorCount = 0;
    for (TActorIterator<AYUFSInteractionDoor> It(World); It; ++It) ++DoorCount;
    TestEqual(TEXT("exactly the original door remains"), DoorCount, 1);

    World->DestroyWorld(false);
    return true;
}

#endif
