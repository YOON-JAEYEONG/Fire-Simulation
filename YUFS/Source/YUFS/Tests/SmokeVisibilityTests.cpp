#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "NPC/YUFSEvacuationNPC.h"
#include "NPC/Perception/YUFSNPCPerceptionComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
struct FSmokeSightFixture
{
    UWorld* World;
    AYUFSEvacuationNPC* NPC;
    UYUFSNPCPerceptionComponent* Sense;
    TSharedRef<FYUFSHazardGrid,ESPMode::ThreadSafe> Data;
    FYUFSHazardSnapshot Field;
    FSmokeSightFixture():Data(MakeShared<FYUFSHazardGrid,ESPMode::ThreadSafe>())
    {
        const auto Settings=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
        World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Settings);
        NPC=World->SpawnActor<AYUFSEvacuationNPC>(); NPC->SetActorLocation(FVector(20,100,90));
        Sense=NPC->GetNPCPerceptionComponent(); Sense->Config=NewObject<UYUFSPerceptionConfig>(Sense);
        Sense->Config->VisionRayCount=1; Sense->Config->VisionRange=1000.f; Sense->Config->MaxProbeStepCm=20.f;
        Data->Dimensions=FIntVector(60,40,40); Data->Density.Init(0,60*40*40); Data->Temperature.Init(0,60*40*40);
        Field.Grid=Data; Field.GridToWorld=FTransform(FQuat::Identity,FVector::ZeroVector,FVector(20));
        Field.Status=EYUFSHazardDataStatus::Ready; Field.Frame=10;
        Band(false,30,35,255); // heat at600..700cm, beyond local thermal range
    }
    void Band(bool Smoke,int32 Begin,int32 End,uint8 Value,int32 MinZ=0)
    {
        for (int32 X=Begin;X<End;++X) for(int32 Y=0;Y<40;++Y) for(int32 Z=MinZ;Z<40;++Z)
            (Smoke?Data->Density:Data->Temperature)[(X*40+Y)*40+Z]=Value;
    }
    FVector Target() const { return FVector(650,100,NPC->GetPawnViewLocation().Z); }
    ~FSmokeSightFixture() { World->DestroyWorld(false); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSSmokeCurtainSightTest,"YUFS.NPC.Perception.SmokeVisibility.CurtainHidesDistantHeatWithoutHidingSmoke",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSSmokeCurtainSightTest::RunTest(const FString&)
{
    FSmokeSightFixture F; F.Band(true,5,20,255);
    F.Sense->UpdateFromSnapshot(F.Field,0.f);
    TestTrue(TEXT("Foreground smoke is still detected"),F.Sense->GetSmokeInFrontNormalized()>.5f);
    TestEqual(TEXT("Dense foreground smoke hides distant visual heat"),F.Sense->GetHeatInSight(),0.f);
    const auto Knowledge=F.Sense->RestrictToKnowledge(F.Field);
    TestFalse(TEXT("Cells behind opaque smoke are not recorded as newly observed"),Knowledge.IsObservedAt(F.Target()));
    TestEqual(TEXT("Hidden heat cannot leak into personal path knowledge"),Knowledge.Sample(F.Target()).Heat,0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSSmokeVisibilityMemoryTest,"YUFS.NPC.Perception.SmokeVisibility.HiddenEvidenceIsRememberedButNotRefreshed",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSSmokeVisibilityMemoryTest::RunTest(const FString&)
{
    FSmokeSightFixture F; F.Sense->UpdateFromSnapshot(F.Field,0.f);
    TestTrue(TEXT("Clear air exposes the distant heat"),F.Sense->RestrictToKnowledge(F.Field).Sample(F.Target()).Heat>.9f);
    F.Band(true,5,20,255); F.Sense->UpdateFromSnapshot(F.Field,2.f);
    TestTrue(TEXT("Smoke does not erase previously learned danger immediately"),F.Sense->RestrictToKnowledge(F.Field).Sample(F.Target()).Heat>.9f);
    F.Sense->UpdateFromSnapshot(F.Field,40.f);
    TestFalse(TEXT("Occluded evidence cannot refresh itself through smoke"),F.Sense->RestrictToKnowledge(F.Field).IsObservedAt(F.Target()));
    TestEqual(TEXT("Unseen old heat expires by existing memory rules"),F.Sense->RestrictToKnowledge(F.Field).Sample(F.Target()).Heat,0.f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSSmokeThinLayerTest,"YUFS.NPC.Perception.SmokeVisibility.ThinLayersAndCalibrationControl",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSSmokeThinLayerTest::RunTest(const FString&)
{
    FSmokeSightFixture F; F.Sense->Config->MaxProbeStepCm=80.f;
    F.Band(true,5,6,255); // one20cm voxel must not disappear between coarse configured probes
    auto Thin=F.Sense->EvaluateSightToPoint(F.Field,F.Target());
    TestTrue(TEXT("A thin smoke layer dims sight without becoming an opaque wall"),Thin.bReachedTarget && Thin.Transmission>.5f && Thin.Transmission<.95f);
    F.Band(true,5,20,255);
    const auto Thick=F.Sense->EvaluateSightToPoint(F.Field,F.Target());
    TestFalse(TEXT("Accumulated smoke thickness blocks the same endpoint"),Thick.bReachedTarget);
    TestTrue(TEXT("Cutoff shortens the visible segment"),Thick.VisibleDistanceCm<300.f);
    F.Sense->Config->SmokeSightAttenuationPerMeter=0.f;
    const auto Disabled=F.Sense->EvaluateSightToPoint(F.Field,F.Target());
    TestTrue(TEXT("Zero attenuation is an explicit geometric-only comparison mode"),Disabled.bReachedTarget && FMath::IsNearlyEqual(Disabled.Transmission,1.f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSSmokePointObservationTest,"YUFS.NPC.Perception.SmokeVisibility.PointObservationSharesFogFovAndWalls",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSSmokePointObservationTest::RunTest(const FString&)
{
    FSmokeSightFixture F; float Smoke=9.f,Heat=9.f;
    F.Band(true,5,20,255);
    TestFalse(TEXT("Interaction hazard query cannot see through opaque smoke"),F.Sense->SampleObservedHazardFromSnapshot(F.Field,F.Target(),0.f,Smoke,Heat));
    TestTrue(TEXT("Refused query resets outputs and never publishes hidden heat"),Smoke==0.f && Heat==0.f && F.Sense->GetKnownCellCount()==0);
    F.Band(true,5,20,0);
    F.NPC->SetActorRotation(FRotator(0,180,0));
    TestFalse(TEXT("Point lookup cannot inspect a fire behind the observer"),F.Sense->SampleObservedHazardFromSnapshot(F.Field,F.Target(),1.f,Smoke,Heat));
    F.NPC->SetActorRotation(FRotator::ZeroRotator);
    auto* Wall=F.World->SpawnActor<AActor>(); auto* Box=NewObject<UBoxComponent>(Wall);
    Wall->SetRootComponent(Box); Box->SetBoxExtent(FVector(10,400,400)); Box->SetCollisionProfileName(TEXT("BlockAll")); Box->RegisterComponent(); Wall->SetActorLocation(FVector(300,100,200));
    TestFalse(TEXT("The shared point query still stops at an actual wall"),F.Sense->SampleObservedHazardFromSnapshot(F.Field,F.Target(),2.f,Smoke,Heat));
    Wall->Destroy();
    TestTrue(TEXT("Looking at the same point through cleared space acquires real evidence"),F.Sense->SampleObservedHazardFromSnapshot(F.Field,F.Target(),3.f,Smoke,Heat));
    TestEqual(TEXT("Visible endpoint retains raw heat rather than attenuating the exported value"),Heat,1.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSSmokeLowSightTest,"YUFS.NPC.Perception.SmokeVisibility.LowEyesAndLocalExposureRemainIndependent",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSSmokeLowSightTest::RunTest(const FString&)
{
    FSmokeSightFixture F; F.Band(true,0,60,255,5);
    F.Sense->UpdateFromSnapshot(F.Field,0.f);
    const float StandingRange=F.Sense->GetCenterVisibleDistanceCm();
    TestEqual(TEXT("Opaque vision does not suppress actual self smoke"),F.Sense->GetSmokeDensity(),1.f);
    auto* Movement=F.NPC->GetCharacterMovement(); Movement->SetMovementMode(MOVE_Walking); Movement->SetCrouchedHalfHeight(44.f); Movement->Crouch(false);
    TestTrue(TEXT("Fixture uses actual low capsule and eye geometry"),F.NPC->IsCrouched());
    F.Sense->UpdateFromSnapshot(F.Field,1.f);
    TestEqual(TEXT("Clear lower layer changes actual self reading"),F.Sense->GetSmokeDensity(),0.f);
    TestTrue(TEXT("Low eyes see farther under an upper smoke layer"),F.Sense->GetCenterVisibleDistanceCm()>StandingRange+300.f);
    F.Band(true,0,60,255); F.Band(false,3,5,255);
    F.Sense->UpdateFromSnapshot(F.Field,2.f);
    TestTrue(TEXT("Nearby heat can still be felt despite optical opacity"),F.Sense->GetNearbyHeat()>.8f);
    TestEqual(TEXT("No clear lower air is invented inside full-height smoke"),F.Sense->GetSmokeDensity(),1.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSSmokeMissingSightTest,"YUFS.NPC.Perception.SmokeVisibility.MissingCoverageIsNotCertifiedClear",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSSmokeMissingSightTest::RunTest(const FString&)
{
    FSmokeSightFixture F; auto Missing=F.Field; Missing.Status=EYUFSHazardDataStatus::Loading; Missing.Grid.Reset();
    const auto Sight=F.Sense->EvaluateSightToPoint(Missing,F.Target());
    TestFalse(TEXT("A geometric line with absent smoke data is not certified clear"),Sight.bCompleteSmokeData);
    float Smoke=1,Heat=1;
    TestFalse(TEXT("Missing frame cannot be published as a new visual observation"),F.Sense->SampleObservedHazardFromSnapshot(Missing,F.Target(),0.f,Smoke,Heat));
    TestTrue(TEXT("Unavailable output is explicitly rejected and zeroed"),Smoke==0 && Heat==0);
    auto Unknown=F.Sense->RestrictToKnowledge(F.Field);
    TestFalse(TEXT("Personal unseen zero values cannot certify optical coverage"),F.Sense->EvaluateSightToPoint(Unknown,F.Target()).bCompleteSmokeData);
    return true;
}
#endif
