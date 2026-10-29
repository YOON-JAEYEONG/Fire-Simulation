#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Animation/YUFSActionAnimationComponent.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "Core/YUFSObservation.h"
#include "Level/YUFSLevelDataManager.h"
#include "Fire/YUFSBinaryManager.h"
#include "Simulation/YUFSSimulationController.h"
#include "Simulation/YUFSTimelineRecorder.h"

struct FYUFSIncapacitationTestAccess
{
    static void Wire(AYUFSSimulationController* Controller, UWorld* World)
    {
        Controller->CachedLDM=World->SpawnActor<AYUFSLevelDataManager>();
        Controller->BinaryManager=World->SpawnActor<AYUFSBinaryManager>();
    }
    static void Count(AYUFSSimulationController* Controller) { Controller->UpdateLiveCounts(); }
};

namespace
{
struct FIncapacitationFixture
{
    UWorld* World;
    AYUFSEvacuationNPC* NPC;
    AYUFSSimulationController* Controller;
    FIncapacitationFixture()
    {
        const auto Settings=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
        World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Settings);
        NPC=World->SpawnActor<AYUFSEvacuationNPC>();
        NPC->GetMesh()->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/NPCs/Crawling__1_.Crawling__1_")));
        NPC->GetActionAnimationComponent()->Initialize(NPC->GetMesh(),37);
        NPC->GetMesh()->SetComponentTickEnabled(true);
        NPC->GetBehaviorStateMachine()->Config=NewObject<UYUFSBehaviorConfig>(NPC);
        NPC->SetActorTickEnabled(true);
        NPC->GetCharacterMovement()->SetComponentTickEnabled(true);
        NPC->GetNavigator()->SetComponentTickEnabled(true);
        Controller=World->SpawnActor<AYUFSSimulationController>();
        FYUFSIncapacitationTestAccess::Wire(Controller,World);
        Controller->RegisterNPC(NPC);
    }
    void Incapacitate()
    {
        FYUFSNPCObservation Smoke; Smoke.SmokeDensityAtSelf=1.f;
        NPC->GetBehaviorStateMachine()->TickStateMachine(40.f,Smoke);
        // Deliberately count before an NPC Tick: controller/actor ordering must not hide or miss the terminal clip.
        FYUFSIncapacitationTestAccess::Count(Controller);
    }
    ~FIncapacitationFixture() { World->DestroyWorld(false); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSIncapacitationPresentationTest,"YUFS.NPC.Integration.Incapacitation.VisibleStationaryAndCountedOnce",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSIncapacitationPresentationTest::RunTest(const FString&)
{
    FIncapacitationFixture F;
    const auto LastPolicyAction=F.NPC->GetLastAction();
    F.NPC->SetActionAnimationPreview(EYUFSAction::Film);
    F.NPC->AddMovementInput(FVector::ForwardVector,1.f);
    F.NPC->GetCharacterMovement()->Velocity=FVector(100,0,0);
    F.Incapacitate();
    TestTrue(TEXT("Real exposure fixture reaches incapacitation"),F.NPC->GetBehaviorStateMachine()->IsIncapacitated());
    TestEqual(TEXT("Incapacitation is counted immediately"),F.Controller->GetIncapacitatedCount(),1);
    TestEqual(TEXT("Terminal presentation does not rewrite the action in the final learning transition"),F.NPC->GetLastAction(),LastPolicyAction);
    TestFalse(TEXT("An incapacitated resident remains visible"),F.NPC->IsHidden());
    TestEqual(TEXT("Terminal native clip overrides a presentation preview even before NPC Tick"),F.NPC->GetActionAnimationComponent()->GetActiveAnimationName(),FString(TEXT("Dying")));
    auto* Single=F.NPC->GetMesh()->GetSingleNodeInstance();
    if (!TestNotNull(TEXT("Compatible native playback exists"),Single)) return false;
    TestFalse(TEXT("The collapse clip is non-looping"),Single->IsLooping());
    TestTrue(TEXT("Animation component keeps ticking after decisions stop"),F.NPC->GetMesh()->IsComponentTickEnabled());
    TestFalse(TEXT("Live decision tick stops"),F.NPC->IsActorTickEnabled());
    TestFalse(TEXT("Navigator retry tick stops"),F.NPC->GetNavigator()->IsComponentTickEnabled());
    TestFalse(TEXT("Root motion cannot move the resolved resident"),F.NPC->GetCharacterMovement()->IsComponentTickEnabled());
    TestFalse(TEXT("Existing no-collision terminal policy is retained"),F.NPC->GetActorEnableCollision());
    TestTrue(TEXT("Residual velocity and movement input are removed"),F.NPC->GetVelocity().IsNearlyZero() && F.NPC->GetPendingMovementInputVector().IsNearlyZero());
    TestFalse(TEXT("Terminal resident is not an RVO obstacle"),F.NPC->GetCharacterMovement()->bUseRVOAvoidance);
    Single->SetPosition(.4f,false);
    FYUFSIncapacitationTestAccess::Count(F.Controller);
    F.NPC->NotifyEpisodeFinished(EYUFSTerminalReason::Incapacitated);
    TestEqual(TEXT("Repeated resolution does not count twice"),F.Controller->GetIncapacitatedCount(),1);
    TestTrue(TEXT("Repeated terminal notification does not restart the collapse"),FMath::IsNearlyEqual(Single->GetCurrentTime(),.4f,.001f));
    // This isolated world has no renderer/component evaluation pass. Complete the
    // instance's normal deferred update explicitly; endpoint playback stops on the next update.
    for (float Delta : {20.f,.01f})
    {
        Single->UpdateAnimation(Delta,false);
        if (Single->NeedsUpdate()) { Single->ParallelUpdateAnimation(); Single->PostUpdateAnimation(); }
    }
    TestTrue(TEXT("The non-looping clip holds its final pose"),FMath::IsNearlyEqual(Single->GetCurrentTime(),Single->GetLength(),.001f));
    TestFalse(TEXT("Completed collapse does not resume playing"),Single->IsPlaying());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSIncapacitationReplayTest,"YUFS.NPC.Integration.Incapacitation.PauseAndHistoricalPose",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSIncapacitationReplayTest::RunTest(const FString&)
{
    FIncapacitationFixture F;
    auto* Recorder=NewObject<UYUFSTimelineRecorder>(F.NPC); Recorder->RegisterComponent(); Recorder->Initialize(nullptr,nullptr);
    Recorder->BeginRecording(10.f,.1f);
    auto* AliveClip=F.NPC->GetMesh()->GetSingleNodeInstance()->GetAnimationAsset();
    Recorder->TickRecording(.2f,0.f,0,{F.NPC},0,0);
    F.Incapacitate();
    auto* Single=F.NPC->GetMesh()->GetSingleNodeInstance(); Single->SetPosition(.4f,false);
    F.NPC->SetLiveSimulationPaused(true);
    TestTrue(TEXT("Pause freezes the visible terminal animation"),F.NPC->GetMesh()->bPauseAnims);
    F.NPC->SetLiveSimulationPaused(false);
    TestFalse(TEXT("Resume releases the terminal animation clock"),F.NPC->GetMesh()->bPauseAnims);
    TestFalse(TEXT("Resume cannot restart terminal decisions"),F.NPC->IsActorTickEnabled());
    TestFalse(TEXT("Resume cannot restart terminal movement"),F.NPC->GetCharacterMovement()->IsComponentTickEnabled());
    Recorder->TickRecording(.2f,1.f,8,{F.NPC},0,1);
    const auto Terminal=F.NPC->BuildTimelineSnapshot();
    TestTrue(TEXT("A visible incapacitated body is not an evacuated actor"),Terminal.bVisible && Terminal.bIncapacitated && !Terminal.bEvacuated);
    Recorder->EnterReviewMode({F.NPC}); Recorder->SeekToFireTime(0.f,{F.NPC});
    TestFalse(TEXT("Earlier healthy actor remains visible"),F.NPC->IsHidden());
    TestTrue(TEXT("Rewind restores healthy actor collision"),F.NPC->GetActorEnableCollision());
    TestEqual(TEXT("Rewind restores the earlier healthy clip"),F.NPC->GetMesh()->GetSingleNodeInstance()->GetAnimationAsset(),AliveClip);
    TestEqual(TEXT("Earlier recorded casualty count is zero"),Recorder->GetCurrentReviewFrameSnapshot().IncapacitatedCount,0);
    Recorder->SeekToFireTime(1.f,{F.NPC});
    Single=F.NPC->GetMesh()->GetSingleNodeInstance();
    TestFalse(TEXT("Recorded incapacitated actor remains visible"),F.NPC->IsHidden());
    TestFalse(TEXT("Visible replay body retains the live terminal no-collision policy"),F.NPC->GetActorEnableCollision());
    TestEqual(TEXT("Review restores the actual collapse clip"),Single->GetAnimationAsset(),Terminal.AnimationClip.Get());
    TestTrue(TEXT("Review restores the exact collapse cursor"),FMath::IsNearlyEqual(Single->GetCurrentTime(),.4f,.001f));
    F.NPC->Tick(.3f);
    TestTrue(TEXT("Review actor tick does not advance or override the terminal pose"),FMath::IsNearlyEqual(Single->GetCurrentTime(),.4f,.001f) && !Single->IsPlaying());
    TestEqual(TEXT("Historical resolution has no live recount side effect"),F.Controller->GetIncapacitatedCount(),1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSIncapacitationExternalTest,"YUFS.NPC.Integration.Incapacitation.PreservesExternalMotionDriver",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSIncapacitationExternalTest::RunTest(const FString&)
{
    FIncapacitationFixture F; F.NPC->bUseExternalMotionDriver=true;
    const auto Mode=F.NPC->GetMesh()->GetAnimationMode();
    auto* Clip=F.NPC->GetMesh()->GetSingleNodeInstance()->GetAnimationAsset();
    F.Incapacitate();
    TestFalse(TEXT("External motion owner also retains a visible resident"),F.NPC->IsHidden());
    TestEqual(TEXT("Native resolution never replaces the external animation mode"),F.NPC->GetMesh()->GetAnimationMode(),Mode);
    TestEqual(TEXT("External owner keeps its animation asset"),F.NPC->GetMesh()->GetSingleNodeInstance()->GetAnimationAsset(),Clip);
    TestNull(TEXT("External driver is not misrecorded as an owned native clip"),F.NPC->BuildTimelineSnapshot().AnimationClip.Get());
    TestFalse(TEXT("External animation ownership does not keep live navigation running"),F.NPC->GetNavigator()->IsComponentTickEnabled());
    return true;
}
#endif
