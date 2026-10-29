#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Animation/YUFSActionAnimationComponent.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "Simulation/YUFSSimulationController.h"
#include "Simulation/YUFSTimelineRecorder.h"
#include "Fire/YUFSInteractionDoor.h"
#include "Fire/YUFSFireExtinguisher.h"

namespace
{
struct FLifecycleFixture
{
    UWorld* World;
    AYUFSEvacuationNPC* NPC;
    UYUFSTimelineRecorder* Recorder;
    FLifecycleFixture()
    {
        const auto Settings=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
        World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Settings);
        FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        NPC=World->SpawnActor<AYUFSEvacuationNPC>(FVector(140,50,90),FRotator::ZeroRotator,Spawn);
        NPC->GetMesh()->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/NPCs/Crawling__1_.Crawling__1_")));
        NPC->GetActionAnimationComponent()->Initialize(NPC->GetMesh(),37);
        Recorder=NewObject<UYUFSTimelineRecorder>(NPC); Recorder->RegisterComponent(); Recorder->Initialize(nullptr,nullptr);
        Recorder->BeginRecording(10.f,.1f);
    }
    void Capture(float T) { Recorder->TickRecording(.2f,T,FMath::RoundToInt(T*8),{NPC},0,0); }
    ~FLifecycleFixture() { World->DestroyWorld(false); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSLivePauseLifecycleTest,"YUFS.NPC.Integration.PauseFreezesAllOwnedClocks",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSLivePauseLifecycleTest::RunTest(const FString&)
{
    FLifecycleFixture F; auto* Controller=F.World->SpawnActor<AYUFSSimulationController>();
    Controller->bEnableTimelineRecording=false; Controller->FireStartDelaySeconds=0.f; Controller->RegisterNPC(F.NPC);
	F.NPC->GetNavigator()->SetComponentTickEnabled(true);
	F.NPC->SetActorTickEnabled(true);
	F.NPC->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    Controller->StartSimulation(); Controller->Tick(.1f);
    Controller->PauseSimulation();
    TestFalse(TEXT("Paused NPC decision tick stops"),F.NPC->IsActorTickEnabled());
    TestTrue(TEXT("Paused skeletal animation clock stops too"),F.NPC->GetMesh()->bPauseAnims);
    TestFalse(TEXT("Paused navigation retry clock stops"),F.NPC->GetNavigator()->IsComponentTickEnabled());
    Controller->PauseSimulation(); Controller->ResumeSimulation();
    TestTrue(TEXT("Resume restores decision tick after repeated Pause"),F.NPC->IsActorTickEnabled());
    TestFalse(TEXT("Resume restores the previous animation pause flag"),F.NPC->GetMesh()->bPauseAnims);
    TestTrue(TEXT("Resume restores navigation retry clock"),F.NPC->GetNavigator()->IsComponentTickEnabled());
	TestEqual(TEXT("Resume does not replace a falling movement mode with walking"),F.NPC->GetCharacterMovement()->MovementMode.GetValue(),MOVE_Falling);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSExternalPauseLifecycleTest,"YUFS.NPC.Integration.ExternalDriverAndDisabledClocksArePreserved",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSExternalPauseLifecycleTest::RunTest(const FString&)
{
    FLifecycleFixture F;
    F.NPC->bUseExternalMotionDriver=true; F.NPC->bUseExternalNavigationDriver=true;
    F.NPC->SetActorTickEnabled(false); F.NPC->GetNavigator()->SetComponentTickEnabled(false);
    F.NPC->GetCharacterMovement()->SetComponentTickEnabled(false); F.NPC->GetMesh()->bPauseAnims=true;
    const auto Mode=F.NPC->GetMesh()->GetAnimationMode();
    F.NPC->SetLiveSimulationPaused(true); F.NPC->SetLiveSimulationPaused(true); F.NPC->SetLiveSimulationPaused(false);
    TestFalse(TEXT("An externally disabled decision tick stays disabled"),F.NPC->IsActorTickEnabled());
    TestFalse(TEXT("An externally disabled movement component stays disabled"),F.NPC->GetCharacterMovement()->IsComponentTickEnabled());
    TestFalse(TEXT("An externally disabled navigator stays disabled"),F.NPC->GetNavigator()->IsComponentTickEnabled());
    TestTrue(TEXT("An existing animation hold remains a hold"),F.NPC->GetMesh()->bPauseAnims);
    auto Snapshot=F.NPC->BuildTimelineSnapshot(); TestNull(TEXT("External driver graph is not serialized as an owned native clip"),Snapshot.AnimationClip.Get());
    F.NPC->SetTimelinePlaybackMode(true); F.NPC->ApplyTimelineSnapshot(Snapshot);
    TestEqual(TEXT("Review never replaces the external animation mode"),F.NPC->GetMesh()->GetAnimationMode(),Mode);
    TestTrue(TEXT("External pose is frozen instead of running a live graph during review"),F.NPC->GetMesh()->bPauseAnims);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSTimelineMotionLifecycleTest,"YUFS.NPC.Integration.ReviewRestoresActualInteractionClipAndTime",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSTimelineMotionLifecycleTest::RunTest(const FString&)
{
    FLifecycleFixture F;
    F.NPC->GetActionAnimationComponent()->ApplyAction(EYUFSAction::GatherBelongings,EYUFSBehaviorState::Normal,true);
    auto* Single=F.NPC->GetMesh()->GetSingleNodeInstance(); if(!TestNotNull(TEXT("native playback available"),Single)) return false;
    auto* RecordedAsset=Single->GetAnimationAsset(); Single->SetPosition(.2f,false); F.Capture(0.f);
    F.NPC->GetActionAnimationComponent()->ApplyAction(EYUFSAction::HelpOther,EYUFSBehaviorState::Normal,true);
    F.NPC->SetActorLocation(FVector(300,50,90)); F.Capture(1.f);
    F.Recorder->EnterReviewMode({F.NPC}); F.Recorder->SeekToFireTime(0.f,{F.NPC});
    Single=F.NPC->GetMesh()->GetSingleNodeInstance();
    TestEqual(TEXT("Review restores actual interaction clip rather than policy Idle"),Single->GetAnimationAsset(),RecordedAsset);
    TestTrue(TEXT("Seek restores the recorded clip cursor"),FMath::IsNearlyEqual(Single->GetCurrentTime(),.2f,.001f));
    TestFalse(TEXT("Review pose has no independent live animation clock"),Single->IsPlaying());
    const float Cursor=Single->GetCurrentTime(); F.NPC->Tick(.3f);
    TestTrue(TEXT("Review actor tick does not replace the restored pose"),Single->GetAnimationAsset()==RecordedAsset && FMath::IsNearlyEqual(Single->GetCurrentTime(),Cursor,.001f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSTimelinePropLifecycleTest,"YUFS.NPC.Integration.ReviewRestoresToolWithoutConsumingOrReserving",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSTimelinePropLifecycleTest::RunTest(const FString&)
{
    FLifecycleFixture F; auto* Tool=F.World->SpawnActor<AYUFSFireExtinguisher>();
    TestTrue(TEXT("Tool pickup fixture"),Tool->PickUp(F.NPC,F.NPC->GetMesh(),NAME_None));
    Tool->StartSpraying(F.NPC); Tool->ShowSprayToward(FVector(500,50,80)); Tool->ConsumeAgent(F.NPC,1.f);
    const float Fuel=Tool->GetRemainingAgentNormalized(); const FTransform Held=Tool->GetActorTransform(); F.Capture(0.f);
    Tool->ConsumeAgent(F.NPC,2.f); Tool->Release(F.NPC); Tool->SetActorLocation(FVector(700,0,0)); F.Capture(1.f);
    F.Recorder->EnterReviewMode({F.NPC}); F.Recorder->SeekToFireTime(0.f,{F.NPC});
    TestEqual(TEXT("Review restores the recorded spray appearance"),Tool->GetExtinguisherState(),EYUFSFireExtinguisherState::Spraying);
    TestTrue(TEXT("Review restores held world position without live attachment ownership"),Tool->GetActorTransform().Equals(Held,.01));
    TestEqual(TEXT("Recorded remaining amount is displayed"),Tool->GetRemainingAgentNormalized(),Fuel);
    TestNull(TEXT("Review cannot retain a live reservation owner"),Tool->GetOwnerActor());
    TestFalse(TEXT("A replay prop cannot be reserved as a live tool"),Tool->TryReserve(F.NPC));
    TestEqual(TEXT("Review does not execute agent consumption"),Tool->ConsumeAgent(F.NPC,1.f),0.f);
    F.Recorder->SeekToFireTime(1.f,{F.NPC});
    TestEqual(TEXT("Seeking forward restores released appearance"),Tool->GetExtinguisherState(),EYUFSFireExtinguisherState::Available);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSTimelineDoorLifecycleTest,"YUFS.NPC.Integration.ReviewRestoresDoorWithoutOperatingIt",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSTimelineDoorLifecycleTest::RunTest(const FString&)
{
    FLifecycleFixture F; auto* Door=F.World->SpawnActor<AYUFSInteractionDoor>(); F.Capture(0.f);
    TestTrue(TEXT("Opening fixture has a valid user"),Door->TryUse(F.NPC)); Door->Tick(3.f);
    TestTrue(TEXT("Live opening reaches completion"),Door->IsOpen()); F.Capture(1.f);
    F.Recorder->EnterReviewMode({F.NPC}); F.Recorder->SeekToFireTime(0.f,{F.NPC});
    TestFalse(TEXT("Earlier closed door is restored"),Door->IsOpen());
    TestFalse(TEXT("Replay cannot reserve or operate a door"),Door->TryUse(F.NPC));
    Door->Tick(30.f); TestFalse(TEXT("Tick cannot advance a replay door"),Door->IsOpen());
    F.Recorder->SeekToFireTime(1.f,{F.NPC}); TestTrue(TEXT("Later open appearance is restored"),Door->IsOpen());
    return true;
}
#endif
