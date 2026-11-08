#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "NPC/Navigation/YUFSLocalMovementComponent.h"
#include "NPC/Perception/YUFSNPCPerceptionComponent.h"
#include "Fire/YUFSBinaryManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSJJWInteractionUnavailableTest,
	"YUFS.NPC.Navigation.JJWInteraction.UnavailableIsNotSafe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSJJWInteractionUnavailableTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	ACharacter* Character = World->SpawnActor<ACharacter>();
	auto* Navigator = NewObject<UYUFSSmokeAwareNavigator>(Character);
	Navigator->RegisterComponent();
	const TArray<FVector> FloorPath{FVector::ZeroVector, FVector(500, 0, 0)};
	TestTrue(TEXT("An unavailable field cannot approve optional interaction travel"),
		Navigator->IsKnownPathDangerous(FloorPath));
	TestTrue(TEXT("Unavailable exposure sample cannot approve an interaction position"),
		Navigator->IsKnownLocationDangerous(FVector(500, 0, 120)));
	TestTrue(TEXT("Empty retreat geometry cannot be approved"), Navigator->IsKnownPathDangerous({}));
	const auto* Binary = World->SpawnActor<AYUFSBinaryManager>();
	TestFalse(TEXT("The read-only provenance gate is not confirmed by default"), Binary->IsDatasetAlignmentConfirmed());
	Navigator->ClearPath();
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSJJWInteractionGenerationTest,
	"YUFS.NPC.Navigation.JJWInteraction.GenerationAndKnowledgeReset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSJJWInteractionGenerationTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	ACharacter* Character = World->SpawnActor<ACharacter>();
	auto* Navigator = NewObject<UYUFSSmokeAwareNavigator>(Character);
	Navigator->RegisterComponent();
	auto* Perception = NewObject<UYUFSNPCPerceptionComponent>(Character);
	Perception->RegisterComponent();
	const uint32 BeforeRequest = Navigator->GetRequestGeneration();
	Navigator->RequestPathAsync(FVector(500, 0, 0), 0);
	TestTrue(TEXT("Interaction lifetime reads the existing JJW request generation"),
		Navigator->GetRequestGeneration() > BeforeRequest);
	const uint32 BeforeCancel = Navigator->GetRequestGeneration();
	Navigator->ClearPath();
	TestTrue(TEXT("Cancelling the JJW request invalidates an interaction's old generation"),
		Navigator->GetRequestGeneration() > BeforeCancel);
	Navigator->ResetObservedHazards();
	TestEqual(TEXT("Compatibility reset clears authoritative perception memory"), Perception->GetKnownCellCount(), 0);
	TestEqual(TEXT("Compatibility reset invalidates stale sampled data"),
		Perception->GetDataStatus(), EYUFSHazardDataStatus::MissingData);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSJJWHeldInteractionTrafficTest,
	"YUFS.NPC.Navigation.JJWInteraction.DoorOperatorIsStationaryObstacle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSJJWHeldInteractionTrafficTest::RunTest(const FString&)
{
	TestTrue(TEXT("An ordinary following peer keeps JJW moving right-of-way"),
		UYUFSLocalMovementComponent::IsMovingPeer(true, false));
	TestFalse(TEXT("A door operator retaining its route is a stationary obstacle"),
		UYUFSLocalMovementComponent::IsMovingPeer(true, true));
	TestFalse(TEXT("A stationary peer cannot gain moving right-of-way"),
		UYUFSLocalMovementComponent::IsMovingPeer(false, false));
	TestTrue(TEXT("The stationary door operator blocks the approaching trajectory"),
		UYUFSLocalMovementComponent::TrajectoriesConflict(FVector::ZeroVector, FVector::ForwardVector, 42.f,
			FVector(100, 0, 0), FVector::ZeroVector, 42.f, 220.f));
	TestTrue(TEXT("Releasing contact restores normal route-following classification"),
		UYUFSLocalMovementComponent::IsMovingPeer(true, false));
	return true;
}

#endif
