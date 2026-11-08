#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Simulation/YUFSSimulationController.h"
#include "Simulation/YUFSTimelineRecorder.h"
#include "Simulation/YUFSSimHUD.h"

struct FYUFSReviewHudTestAccess
{
    static void State(AYUFSSimulationController* C,ESimPhase Phase,float Time,int32 Evacuated,int32 Incapacitated,int32 Total)
    { C->CurrentPhase=Phase;C->ElapsedSimTime=Time;C->LiveEvacuatedCount=Evacuated;C->LiveIncapacitatedCount=Incapacitated;C->InitialNPCCount=Total; }
    static void Bind(UYUFSSimHUD* H,AYUFSSimulationController* C) { H->SimController=C; }
    static void Pause(AYUFSSimulationController* C) { C->bIsPaused=true; }
    static void StoreFinal(AYUFSSimulationController* C)
    { FSimRunResult R;R.TotalNPCCount=5;R.EvacuatedCount=3;R.IncapacitatedCount=1;C->AllRunResults.Add(R); }
};
namespace
{
struct FReviewHudFixture
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    AYUFSSimulationController* C=World->SpawnActor<AYUFSSimulationController>();
    UYUFSSimHUD* H=NewObject<UYUFSSimHUD>();
    UYUFSTimelineRecorder* R=C->FindComponentByClass<UYUFSTimelineRecorder>();
    FReviewHudFixture()
    {
        FYUFSReviewHudTestAccess::Bind(H,C);R->Initialize(C,nullptr);R->BeginRecording(60.f,.1f);
        Capture(5.f,35.f,1,0,2);Capture(60.f,90.f,3,1,5);
        FYUFSReviewHudTestAccess::StoreFinal(C);
    }
    void Capture(float FireTime,float SimTime,int32 Evac,int32 Incap,int32 Total)
    { FYUFSReviewHudTestAccess::State(C,ESimPhase::FireActive,SimTime,Evac,Incap,Total);R->TickRecording(.2f,FireTime,FMath::RoundToInt(FireTime*8),{},Evac,Incap); }
    void Review()
    { R->EnterReviewMode({});FYUFSReviewHudTestAccess::State(C,ESimPhase::TimelineReview,90.f,3,1,5); }
    ~FReviewHudFixture() { World->DestroyWorld(false); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSHistoricalHudTest,"YUFS.NPC.Integration.ReviewHUD.HistoricalCountsRateAndTime",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSHistoricalHudTest::RunTest(const FString&)
{
    FReviewHudFixture F;F.Review();F.C->SeekTimelineBySeconds(5.f);
    const FString Text=F.H->GetNPCStatusText().ToString();
    TestTrue(TEXT("Review displays recorded evacuation count"),Text.Contains(TEXT("대피: 1 |")));
    TestTrue(TEXT("Review displays recorded incapacitation, not the final live count"),Text.Contains(TEXT(": 0 |")));
    TestTrue(TEXT("A later participant cannot inflate the historical population"),Text.Contains(TEXT("전체: 2")));
    TestEqual(TEXT("Historical rate uses both historical numerator and denominator"),F.H->GetEvacuationRatePercent(),50.f);
    TestEqual(TEXT("HUD elapsed time includes the recorded prefire period, not the final clock"),F.H->GetElapsedTimeText().ToString(),FString(TEXT("0:35")));
    F.C->SeekTimelineBySeconds(60.f);
    TestTrue(TEXT("Forward seek displays the final snapshot"),F.H->GetNPCStatusText().ToString().Contains(TEXT("전체: 5")));
    TestEqual(TEXT("Forward seek updates the rate"),F.H->GetEvacuationRatePercent(),60.f);
    F.C->SeekTimelineBySeconds(5.f);
    TestEqual(TEXT("Seeking never mutates live evacuation results"),F.C->GetEvacuatedCount(),3);
    TestEqual(TEXT("Seeking never mutates live incapacitation results"),F.C->GetIncapacitatedCount(),1);
    TestEqual(TEXT("Stored final result remains final"),F.C->GetAllRunResults()[0].EvacuatedCount,3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSEmptyHistoricalHudTest,"YUFS.NPC.Integration.ReviewHUD.EmptyAndNewRecordingCannotLeakOldCounts",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSEmptyHistoricalHudTest::RunTest(const FString&)
{
    FReviewHudFixture F;F.Review();F.R->BeginRecording(10.f,.1f);F.R->EnterReviewMode({});
    TestTrue(TEXT("An empty review explicitly reports missing history"),F.H->GetNPCStatusText().ToString().Contains(TEXT("기록 없음")));
    TestEqual(TEXT("An empty review cannot show the final live evacuation rate"),F.H->GetEvacuationRatePercent(),0.f);
    F.R->BeginRecording(10.f,.1f);F.Capture(0.f,2.f,0,0,1);F.Review();F.C->SeekTimelineBySeconds(0.f);
    TestTrue(TEXT("A new recording uses its own population"),F.H->GetNPCStatusText().ToString().Contains(TEXT("전체: 1")));
    TestEqual(TEXT("A new recording uses its own simulation clock"),F.H->GetElapsedTimeText().ToString(),FString(TEXT("0:02")));
    TestEqual(TEXT("A new recording cannot retain old evacuation rate"),F.H->GetEvacuationRatePercent(),0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSLiveHudTest,"YUFS.NPC.Integration.ReviewHUD.LivePausedAndCompletedStayLive",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FYUFSLiveHudTest::RunTest(const FString&)
{
    FReviewHudFixture F;FYUFSReviewHudTestAccess::State(F.C,ESimPhase::FireActive,90.f,3,1,5);
    FYUFSReviewHudTestAccess::Pause(F.C);
    TestEqual(TEXT("A live pause does not show the recorder's historical cursor"),F.H->GetEvacuationRatePercent(),60.f);
    TestEqual(TEXT("The live paused clock stays live"),F.H->GetElapsedTimeText().ToString(),FString(TEXT("1:30")));
    FYUFSReviewHudTestAccess::State(F.C,ESimPhase::Completed,90.f,3,1,5);
    TestEqual(TEXT("Completed mode retains final live statistics"),F.H->GetEvacuationRatePercent(),60.f);
    FYUFSReviewHudTestAccess::State(F.C,ESimPhase::WaitingToStart,0.f,0,0,0);
    TestEqual(TEXT("Zero participants never divides by zero"),F.H->GetEvacuationRatePercent(),0.f);
    return true;
}
#endif
