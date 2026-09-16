#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "NavigationData.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "NPC/Perception/YUFSNPCPerceptionComponent.h"

// JJW's navigator and 8192-cell perception memory are authoritative. This suite
// no longer tests our retired 256-patch navigator cache, direct hazard injection,
// duplicate-request budget, or private preferred-retreat bookkeeping. Physical
// blockage and yielding are covered by JJW's real LocalMovement tests.
struct FYUFSInteractionNavigationTestAccess
{
    UWorld* World = nullptr;
    ACharacter* Character = nullptr;
    UYUFSSmokeAwareNavigator* Navigator = nullptr;
    FYUFSInteractionNavigationTestAccess()
    {
        World = UWorld::CreateWorld(EWorldType::Game, false);
        Character = World->SpawnActor<ACharacter>();
        Navigator = NewObject<UYUFSSmokeAwareNavigator>(Character);
        Navigator->RegisterComponent();
    }
    ~FYUFSInteractionNavigationTestAccess() { Navigator->ClearPath(); World->DestroyWorld(false); }
    uint32 Begin(uint32 Id, FVector Destination)
    {
        Navigator->CancelPendingRequest();
        Navigator->CurrentPath.Reset();
        Navigator->CurrentWaypointIndex = 0;
        Navigator->ActiveQueryId = Id;
        Navigator->RequestedDestination = Navigator->CurrentDestination = Destination;
        Navigator->SetNavigationStatus(EYUFSNavigationStatus::Pathfinding);
        return Navigator->RequestGeneration;
    }
    static FNavPathSharedPtr Path(const TArray<FVector>& Points, bool bPartial = false)
    {
        auto Result = MakeShared<FNavigationPath, ESPMode::ThreadSafe>();
        for (const auto& Point : Points) Result->GetPathPoints().Emplace(Point);
        Result->SetIsPartial(bPartial);
        Result->MarkReady();
        return Result;
    }
    void Deliver(uint32 Id, uint32 Generation, FNavPathSharedPtr P)
    { Navigator->OnPathFound(Id, ENavigationQueryResult::Success, P, Generation); }
    TArray<FVector> Remaining() const { return Navigator->BuildRemainingPath(); }
    void AdvanceRetry() { Navigator->TimeSinceRequest = Navigator->FailedPathRetryInterval; }
    uint32 Generation() const { return Navigator->RequestGeneration; }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSInteractionNavigationCancelTest,
    "YUFS.NPC.Navigation.Interaction.CancelAndLatestDestination",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSInteractionNavigationCancelTest::RunTest(const FString&)
{
    FYUFSInteractionNavigationTestAccess F;
    const FVector Approach(500, 0, 0), Exit(0, 500, 0);
    const uint32 First = F.Begin(11, Approach);
    F.Character->GetCharacterMovement()->Velocity = FVector(300, 0, 0);
    F.Navigator->ClearPath();
    F.Deliver(11, First, F.Path({FVector::ZeroVector, Approach}));
    TestTrue(TEXT("Cancelled suppression approach cannot reappear"), F.Navigator->GetCurrentPathPoints().IsEmpty());
    TestTrue(TEXT("Cancellation consumes physical movement"), F.Character->GetVelocity().IsNearlyZero());
    TestFalse(TEXT("Cancellation clears pending flag"), F.Navigator->bIsPathfinding);
    const uint32 Old = F.Begin(12, Approach);
    const uint32 New = F.Begin(13, Exit);
    F.Deliver(12, Old, F.Path({FVector::ZeroVector, Approach}));
    TestTrue(TEXT("Late approach path cannot end newer evacuation request"), F.Navigator->bIsPathfinding);
    F.Deliver(13, New, F.Path({FVector::ZeroVector, Exit}));
    TestEqual(TEXT("Latest exact requested target is retained"), F.Navigator->GetRequestedDestination(), Exit);
    TestTrue(TEXT("Latest complete path is followed"), F.Navigator->IsFollowingPath());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSInteractionNavigationGeometryTest,
    "YUFS.NPC.Navigation.Interaction.CompletePathCornersAndFloors",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSInteractionNavigationGeometryTest::RunTest(const FString&)
{
    FYUFSInteractionNavigationTestAccess F;
    const FVector Corner(500, 0, 0), Goal(500, 500, 0);
    uint32 Gen = F.Begin(21, Goal);
    F.Deliver(21, Gen, F.Path({FVector::ZeroVector, Corner}, true));
    TestEqual(TEXT("Partial path is an explicit failure"), F.Navigator->GetLastFailure(), EYUFSNavigationFailure::PartialPath);
    Gen = F.Begin(22, Goal);
    F.Deliver(22, Gen, F.Path({}));
    TestFalse(TEXT("Empty engine success cannot become moving"), F.Navigator->IsFollowingPath());
    Gen = F.Begin(24, Goal);
    F.Deliver(24, Gen, F.Path({FVector::ZeroVector, Corner, Goal}));
    TestEqual(TEXT("Long lookahead cannot cut a building corner"), F.Navigator->GetSteeringTarget(FVector::ZeroVector, 2000.f), Corner);
    // JJW owns the acceptance policy: the caller explicitly requests 25 cm.
    F.Navigator->UpdateWaypoint(Corner - FVector(60, 0, 0), 25.f);
    TestEqual(TEXT("Caller-selected corner precision is preserved"), F.Navigator->GetCurrentWaypointIndex(), 1);
    F.Navigator->UpdateWaypoint(Corner, 25.f);
    F.Character->SetActorLocation(Corner + FVector(0, 0, F.Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
    TestEqual(TEXT("Final hazard segment includes feet to goal"), F.Remaining().Num(), 2);
    F.Navigator->UpdateWaypoint(Goal + FVector(0, 0, 400), 25.f);
    TestTrue(TEXT("Matching XY on another floor is not arrival"), F.Navigator->IsFollowingPath());
    F.Navigator->UpdateWaypoint(Goal, 25.f);
    TestEqual(TEXT("Physical arrival stops navigation"), F.Navigator->GetNavigationStatus(), EYUFSNavigationStatus::Arrived);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSInteractionNavigationKnowledgeTest,
    "YUFS.NPC.Navigation.Interaction.PersonalKnowledgeSnapshot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSInteractionNavigationKnowledgeTest::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    auto MakeObserver = [&]()
    {
        auto* Actor = World->SpawnActor<ACharacter>();
        Actor->SetActorLocation(FVector(100, 100, 100));
        auto* Perception = NewObject<UYUFSNPCPerceptionComponent>(Actor);
        Perception->RegisterComponent();
        Perception->Config = NewObject<UYUFSPerceptionConfig>(Perception);
        Perception->Config->VisionRayCount = 1;
        Perception->Config->VisionRange = 500.f;
        return Perception;
    };
    auto* A = MakeObserver();
    auto* B = MakeObserver();
    B->GetOwner()->SetActorLocation(FVector(700, 300, 100));
    auto Data = MakeShared<FYUFSHazardGrid, ESPMode::ThreadSafe>();
    Data->Dimensions = FIntVector(20, 10, 12);
    Data->Density.Init(0, 2400);
    Data->Temperature.Init(0, 2400);
    for (int32 X = 5; X < 8; ++X)
        for (int32 Y = 0; Y < 10; ++Y)
            for (int32 Z = 0; Z < 5; ++Z)
                Data->Temperature[(X * 10 + Y) * 12 + Z] = 255;
    FYUFSHazardSnapshot Raw;
    Raw.Grid = Data;
    Raw.GridToWorld = FTransform(FQuat::Identity, FVector::ZeroVector, FVector(40));
    Raw.Status = EYUFSHazardDataStatus::Ready;
    Raw.Frame = 1;
    FYUFSHazardSettings Settings;
    const FVector Hot(240, 100, 120);
    const TArray<FVector> Route{FVector(100, 100, 0), FVector(500, 100, 0)};
    TestEqual(TEXT("Unseen global heat is not NPC knowledge"), A->RestrictToKnowledge(Raw).Sample(Hot).Heat, 0.f);
    A->UpdateFromSnapshot(Raw, 0.f);
    const auto Frozen = A->RestrictToKnowledge(Raw);
    TestTrue(TEXT("Personally observed heat contributes to the route"), Frozen.ScorePath(Route, Settings).bUnsafeAhead);
    TestEqual(TEXT("Another NPC does not silently gain private observations"), B->RestrictToKnowledge(Raw).Sample(Hot).Heat, 0.f);
    TestTrue(TEXT("Observed heat has a positive query cost"), Frozen.SegmentAddedCost(Route[0], Route[1], Settings) > 0.f);
    const TArray<FVector> Upstairs{Route[0] + FVector(0, 0, 300), Route[1] + FVector(0, 0, 300)};
    TestFalse(TEXT("Known heat is not projected through the upstairs floor"), Frozen.ScorePath(Upstairs, Settings).bUnsafeAhead);

    auto ClearData = MakeShared<FYUFSHazardGrid, ESPMode::ThreadSafe>();
    ClearData->Dimensions = Data->Dimensions;
    ClearData->Density.Init(0, 2400);
    ClearData->Temperature.Init(0, 2400);
    FYUFSHazardSnapshot Clear = Raw;
    Clear.Grid = ClearData;
    Clear.Frame = 2;
    A->UpdateFromSnapshot(Clear, 1.f);
    TestEqual(TEXT("Fresh clear observation removes obsolete cells"), A->GetKnownCellCount(), 0);
    TestTrue(TEXT("In-flight snapshot does not change when memory updates"), Frozen.ScorePath(Route, Settings).bUnsafeAhead);

    Raw.Frame = 3;
    A->UpdateFromSnapshot(Raw, 2.f);
    A->GetOwner()->SetActorLocation(FVector(700, 100, 100));
    A->UpdateFromSnapshot(Raw, 40.f);
    TestEqual(TEXT("Unobserved hazard memory expires under JJW policy"), A->GetKnownCellCount(), 0);
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSInteractionNavigationRetryTest,
    "YUFS.NPC.Navigation.Interaction.MissingNavmeshBoundedRetry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSInteractionNavigationRetryTest::RunTest(const FString&)
{
    FYUFSInteractionNavigationTestAccess F;
    F.Navigator->RequestPathAsync(FVector::ZeroVector, 0);
    TestEqual(TEXT("Origin is valid; missing navigation is explicit"), F.Navigator->GetLastFailure(), EYUFSNavigationFailure::NoNavigationData);
    TestFalse(TEXT("Retry cannot run every tick"), F.Navigator->ShouldRetryPath());
    for (int32 I = 0; I < F.Navigator->MaxFailedPathRetries; ++I)
    {
        F.AdvanceRetry();
        TestTrue(TEXT("Retry is admitted after the shared cooldown"), F.Navigator->ShouldRetryPath());
        F.Navigator->ReplanPath(I + 1, EYUFSRepathReason::Retry);
    }
    F.AdvanceRetry();
    TestFalse(TEXT("An unreachable interaction goal has bounded retries"), F.Navigator->ShouldRetryPath());
    const uint32 Exhausted = F.Generation();
    F.Navigator->RequestPathAsync(FVector(0, 500, 0), 50);
    TestTrue(TEXT("A new evacuation goal can replace a failed interaction"), F.Generation() > Exhausted);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSInteractionNavigationPhysicalCancelTest,
    "YUFS.NPC.Navigation.Interaction.CancellationPreservesPhysicalActor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSInteractionNavigationPhysicalCancelTest::RunTest(const FString&)
{
    FYUFSInteractionNavigationTestAccess F;
    const FVector Position = F.Character->GetActorLocation();
    const uint32 Generation = F.Begin(51, FVector(500, 0, 0));
    F.Deliver(51, Generation, F.Path({FVector::ZeroVector, FVector(500, 0, 0)}));
    F.Character->GetCharacterMovement()->Velocity = FVector(300, 0, 0);
    F.Navigator->ClearPath();
    TestEqual(TEXT("Cancelling an interaction cannot teleport the person"), F.Character->GetActorLocation(), Position);
    TestTrue(TEXT("Cancellation leaves physical collision enabled"), F.Character->GetActorEnableCollision());
    TestFalse(TEXT("Cancellation cannot hide a resident to fake success"), F.Character->IsHidden());
    TestTrue(TEXT("Cancellation stops stale movement"), F.Character->GetVelocity().IsNearlyZero());
    return true;
}
#endif
