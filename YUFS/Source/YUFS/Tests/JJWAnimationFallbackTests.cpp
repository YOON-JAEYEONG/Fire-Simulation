#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimationAsset.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "NPC/Animation/YUFSActionAnimationComponent.h"
#include "NPC/Behavior/YUFSBehaviorStateMachine.h"
#include "NPC/Navigation/YUFSSmokeAwareNavigator.h"
#include "NPC/YUFSEvacuationNPC.h"

namespace
{
struct FJJWAnimationFixture
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AYUFSEvacuationNPC* NPC = World->SpawnActor<AYUFSEvacuationNPC>();
	USkeletalMeshComponent* Mesh = NPC->GetMesh();
	UYUFSActionAnimationComponent* Animation = NPC->GetActionAnimationComponent();
	~FJJWAnimationFixture() { World->DestroyWorld(false); }
};
constexpr const TCHAR* MannyMeshPath = TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple");
constexpr const TCHAR* MannyAnimClassPath = TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed.ABP_Unarmed_C");
constexpr const TCHAR* RlMeshPath = TEXT("/Game/NPCs/Crawling__1_.Crawling__1_");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSJJWMannyAnimationFallbackTest,
	"YUFS.NPC.Animation.JJW.IncompatibleDefaultsPreserveOriginalDriver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSJJWMannyAnimationFallbackTest::RunTest(const FString&)
{
	FJJWAnimationFixture F;
	auto* Manny = LoadObject<USkeletalMesh>(nullptr, MannyMeshPath);
	auto* AnimClass = LoadClass<UAnimInstance>(nullptr, MannyAnimClassPath);
	if (!TestNotNull(TEXT("Existing JJW Manny mesh loads"), Manny)
		|| !TestNotNull(TEXT("Existing JJW Animation Blueprint loads"), AnimClass)) return false;
	F.Mesh->SetSkeletalMesh(Manny);
	F.Mesh->SetAnimInstanceClass(AnimClass);
	F.Mesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	UAnimInstance* PreviousInstance = F.Mesh->GetAnimInstance();
	const auto PreviousState = F.NPC->GetBehaviorStateMachine()->GetCurrentState();
	const auto PreviousNavigation = F.NPC->GetNavigator()->GetNavigationStatus();
	TestFalse(TEXT("Cross-skeleton native defaults are rejected before initialization"),
		F.Animation->CanUseNativeAnimations(F.Mesh));
	F.Animation->Initialize(F.Mesh, 123);
	for (uint8 Index = 0; Index <= static_cast<uint8>(EYUFSAction::Film); ++Index)
		F.Animation->ApplyAction(static_cast<EYUFSAction>(Index), EYUFSBehaviorState::Normal, true);
	F.Animation->ApplyAction(EYUFSAction::EvacuateToNearestExit, EYUFSBehaviorState::Crawling, true);
	F.Animation->ApplyAction(EYUFSAction::Cough, EYUFSBehaviorState::Incapacitated, true);
	TestEqual(TEXT("Initialize and all later actions preserve the original Animation Blueprint mode"),
		F.Mesh->GetAnimationMode(), EAnimationMode::AnimationBlueprint);
	TestEqual(TEXT("Original animation class is unchanged"), F.Mesh->GetAnimClass(), AnimClass);
	TestEqual(TEXT("Original animation instance is not replaced"), F.Mesh->GetAnimInstance(), PreviousInstance);
	TestEqual(TEXT("Character avatar is not replaced"), F.Mesh->GetSkeletalMeshAsset(), Manny);
	TestEqual(TEXT("No incompatible native animation became active"), F.Animation->GetActiveAnimationName(), FString(TEXT("None")));
	TestEqual(TEXT("No behavior state mutation"), F.NPC->GetBehaviorStateMachine()->GetCurrentState(), PreviousState);
	TestEqual(TEXT("No navigation mutation"), F.NPC->GetNavigator()->GetNavigationStatus(), PreviousNavigation);
	TestFalse(TEXT("Fallback does not rewrite the team's external motion flag"), F.NPC->bUseExternalMotionDriver);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSJJWRlNativeAnimationTest,
	"YUFS.NPC.Animation.JJW.CompatibleRLPreservesNativeActions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSJJWRlNativeAnimationTest::RunTest(const FString&)
{
	FJJWAnimationFixture F;
	auto* RL = LoadObject<USkeletalMesh>(nullptr, RlMeshPath);
	if (!TestNotNull(TEXT("Existing RL avatar loads"), RL)) return false;
	F.Mesh->SetSkeletalMesh(RL);
	TestTrue(TEXT("All effective 11 actions and both physical states are compatible"),
		F.Animation->CanUseNativeAnimations(F.Mesh));
	F.Animation->Initialize(F.Mesh, 456);
	TestEqual(TEXT("Compatible avatar still starts its native idle"), F.Animation->GetActiveAnimationName(), FString(TEXT("Idle")));
	for (uint8 Index = 0; Index <= static_cast<uint8>(EYUFSAction::Film); ++Index)
	{
		const auto Action = static_cast<EYUFSAction>(Index);
		F.Animation->ApplyAction(Action, EYUFSBehaviorState::Normal, true);
		TestEqual(TEXT("Compatible action keeps single-node presentation"),
			F.Mesh->GetAnimationMode(), EAnimationMode::AnimationSingleNode);
		TestTrue(TEXT("Configured action actually selected an animation"), F.Animation->GetActiveAnimationName() != TEXT("None"));
	}
	F.Animation->ApplyAction(EYUFSAction::EvacuateToNearestExit, EYUFSBehaviorState::Crawling, true);
	TestEqual(TEXT("Crawling override still plays"), F.Animation->GetActiveAnimationName(), FString(TEXT("Crawling__1__Anim")));
	F.Animation->ApplyAction(EYUFSAction::Cough, EYUFSBehaviorState::Incapacitated, true);
	TestEqual(TEXT("Incapacitation override still plays"), F.Animation->GetActiveAnimationName(), FString(TEXT("Dying")));
	TestEqual(TEXT("Compatible avatar is not changed"), F.Mesh->GetSkeletalMeshAsset(), RL);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FYUFSJJWCustomAnimationBindingTest,
	"YUFS.NPC.Animation.JJW.CustomBindingsAndPhysicalStatesAreChecked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FYUFSJJWCustomAnimationBindingTest::RunTest(const FString&)
{
	FJJWAnimationFixture F;
	auto* Manny = LoadObject<USkeletalMesh>(nullptr, MannyMeshPath);
	auto* Compatible = LoadObject<UAnimationAsset>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"));
	auto* OtherSkeleton = LoadObject<UAnimationAsset>(nullptr, TEXT("/Game/NPCs/Idle.Idle"));
	if (!TestNotNull(TEXT("Custom-binding avatar exists"), Manny)
		|| !TestNotNull(TEXT("Existing compatible custom sequence loads"), Compatible)
		|| !TestNotNull(TEXT("Cross-skeleton counterexample exists"), OtherSkeleton)) return false;
	F.Mesh->SetSkeletalMesh(Manny);
	// Test configuration only: no asset is created, modified, retargeted, or saved.
	for (auto& Binding : F.Animation->ActionAnimations) Binding.Animation = Compatible;
	F.Animation->CrawlingAnimation = Compatible;
	F.Animation->IncapacitatedAnimation = Compatible;
	TestTrue(TEXT("Explicit compatible custom bindings are accepted on a different avatar"),
		F.Animation->CanUseNativeAnimations(F.Mesh));
	F.Animation->ActionAnimations.Last().Animation = OtherSkeleton;
	TestFalse(TEXT("Probe checks the final action, not only Idle"), F.Animation->CanUseNativeAnimations(F.Mesh));
	F.Animation->ActionAnimations.Last().Animation = Compatible;
	F.Animation->CrawlingAnimation = OtherSkeleton;
	TestFalse(TEXT("Probe includes crawling"), F.Animation->CanUseNativeAnimations(F.Mesh));
	F.Animation->CrawlingAnimation = Compatible;
	F.Animation->IncapacitatedAnimation = OtherSkeleton;
	TestFalse(TEXT("Probe includes incapacitation"), F.Animation->CanUseNativeAnimations(F.Mesh));
	F.Animation->IncapacitatedAnimation = Compatible;
	F.Animation->ActionAnimations.RemoveAt(F.Animation->ActionAnimations.Num() - 1);
	TestFalse(TEXT("A missing required action cannot take over the original driver"), F.Animation->CanUseNativeAnimations(F.Mesh));
	return true;
}

#endif
