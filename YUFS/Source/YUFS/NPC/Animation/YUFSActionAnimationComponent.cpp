#include "NPC/Animation/YUFSActionAnimationComponent.h"

#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimationAsset.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

namespace
{
	// The placed NPCs use /Game/NPCs/Crawling__1__Skeleton. Use the original
	// sequences that share that skeleton; the RTA_* copies target Manny and are
	// intentionally not compatible with the current map actors.
	constexpr const TCHAR* IdlePath = TEXT("/Game/NPCs/Idle.Idle");
	constexpr const TCHAR* RunPath = TEXT("/Game/NPCs/Fast_Run.Fast_Run");
	constexpr const TCHAR* WalkPath = TEXT("/Game/NPCs/Walking.Walking");
	constexpr const TCHAR* CoughPath = TEXT("/Game/NPCs/Standing_Cough_Combined_1.Standing_Cough_Combined_1");
	constexpr const TCHAR* TabletPath = TEXT("/Game/NPCs/Standing_Using_Touchscreen_Tablet.Standing_Using_Touchscreen_Tablet");
	constexpr const TCHAR* TalkingPath = TEXT("/Game/NPCs/Talking.Talking");
	constexpr const TCHAR* CrawlingPath = TEXT("/Game/NPCs/Crawling__1__Anim.Crawling__1__Anim");
	constexpr const TCHAR* DyingPath = TEXT("/Game/NPCs/Dying.Dying");
}

UYUFSActionAnimationComponent::UYUFSActionAnimationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// Every V1 action has an explicit binding. Actions in the same locomotion
	// family deliberately share a sequence but use different rates so the
	// semantic mapping remains stable without duplicating animation assets.
	AddDefaultBinding(EYUFSAction::Idle, IdlePath, true, 1.f);
	AddDefaultBinding(EYUFSAction::SeekInformation, IdlePath, true, 0.82f);
	AddDefaultBinding(EYUFSAction::AlertNearbyOccupants, TalkingPath, true, 1.12f);
	AddDefaultBinding(EYUFSAction::GatherBelongings, TabletPath, true, 0.72f);
	AddDefaultBinding(EYUFSAction::EvacuateToNearestExit, RunPath, true, 1.f);
	AddDefaultBinding(EYUFSAction::EvacuateToFamiliarExit, RunPath, true, 0.92f);
	AddDefaultBinding(EYUFSAction::HelpOther, WalkPath, true, 0.85f);
	AddDefaultBinding(EYUFSAction::WaitForInfo, IdlePath, true, 0.58f);
	AddDefaultBinding(EYUFSAction::Cough, CoughPath, true, 1.f);
	AddDefaultBinding(EYUFSAction::FollowCrowd, RunPath, true, 1.08f);
	AddDefaultBinding(EYUFSAction::Film, TabletPath, true, 1.f);

	CrawlingAnimation = TSoftObjectPtr<UAnimationAsset>(FSoftObjectPath(CrawlingPath));
	IncapacitatedAnimation = TSoftObjectPtr<UAnimationAsset>(FSoftObjectPath(DyingPath));
}

void UYUFSActionAnimationComponent::Initialize(USkeletalMeshComponent* InMesh, int32 StableNpcId)
{
	Mesh = InMesh;
	StableId = StableNpcId;
	bInitialized = IsValid(Mesh);
	ApplyAction(EYUFSAction::Idle, EYUFSBehaviorState::Normal, true);
}

void UYUFSActionAnimationComponent::ApplyAction(
	EYUFSAction Action,
	EYUFSBehaviorState BehaviorState,
	bool bForce)
{
	if (!bInitialized || !IsValid(Mesh))
	{
		return;
	}

	if (!bForce && ActiveAction == Action && ActiveBehaviorState == BehaviorState && IsValid(ActiveAnimation))
	{
		return;
	}

	TSoftObjectPtr<UAnimationAsset> AnimationReference;
	bool bLoop = true;
	float PlayRate = 1.f;

	if (BehaviorState == EYUFSBehaviorState::Incapacitated)
	{
		AnimationReference = IncapacitatedAnimation;
		bLoop = false;
	}
	else if (BehaviorState == EYUFSBehaviorState::Crawling)
	{
		AnimationReference = CrawlingAnimation;
		PlayRate = 0.85f;
	}
	else if (const FYUFSActionAnimationBinding* Binding = FindBinding(Action))
	{
		AnimationReference = Binding->Animation;
		bLoop = Binding->bLoop;
		PlayRate = Binding->PlayRate;
	}

	UAnimationAsset* Animation = AnimationReference.LoadSynchronous();
	if (!Animation)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[YUFS][Animation] Missing animation for action %s (%s)."),
			*StaticEnum<EYUFSAction>()->GetNameStringByValue(static_cast<int64>(Action)),
			*AnimationReference.ToSoftObjectPath().ToString());
		return;
	}

	const USkeletalMesh* SkeletalMesh = Mesh->GetSkeletalMeshAsset();
	if (SkeletalMesh && Animation->GetSkeleton() && SkeletalMesh->GetSkeleton() != Animation->GetSkeleton())
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("[YUFS][Animation] Skeleton mismatch: mesh %s cannot play %s."),
			*SkeletalMesh->GetName(),
			*Animation->GetName());
		return;
	}

	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Mesh->PlayAnimation(Animation, bLoop);

	if (UAnimSingleNodeInstance* SingleNode = Mesh->GetSingleNodeInstance())
	{
		SingleNode->SetPlayRate(FMath::Max(PlayRate, 0.1f));
		if (bLoop)
		{
			if (const UAnimSequenceBase* Sequence = Cast<UAnimSequenceBase>(Animation))
			{
				const float Fraction = FMath::Frac(FMath::Abs(static_cast<float>(StableId)) * 0.61803398875f);
				const float StartOffset = Sequence->GetPlayLength()
					* FMath::Clamp(MaximumLoopStartOffsetFraction, 0.f, 1.f)
					* Fraction;
				SingleNode->SetPosition(StartOffset, false);
			}
		}
	}

	ActiveAnimation = Animation;
	ActiveAction = Action;
	ActiveBehaviorState = BehaviorState;

	UE_LOG(
		LogTemp,
		Verbose,
		TEXT("[YUFS][Animation] %s -> %s (rate %.2f, loop %s)."),
		*StaticEnum<EYUFSAction>()->GetNameStringByValue(static_cast<int64>(Action)),
		*Animation->GetName(),
		PlayRate,
		bLoop ? TEXT("true") : TEXT("false"));
}

bool UYUFSActionAnimationComponent::HasAnimationForAction(EYUFSAction Action) const
{
	const FYUFSActionAnimationBinding* Binding = FindBinding(Action);
	return Binding && !Binding->Animation.IsNull();
}

FString UYUFSActionAnimationComponent::GetActiveAnimationName() const
{
	return IsValid(ActiveAnimation) ? ActiveAnimation->GetName() : TEXT("None");
}

FString UYUFSActionAnimationComponent::GetConfiguredAnimationPath(EYUFSAction Action) const
{
	const FYUFSActionAnimationBinding* Binding = FindBinding(Action);
	return Binding ? Binding->Animation.ToSoftObjectPath().ToString() : FString();
}

const FYUFSActionAnimationBinding* UYUFSActionAnimationComponent::FindBinding(EYUFSAction Action) const
{
	return ActionAnimations.FindByPredicate([Action](const FYUFSActionAnimationBinding& Binding)
	{
		return Binding.Action == Action;
	});
}

void UYUFSActionAnimationComponent::AddDefaultBinding(
	EYUFSAction Action,
	const TCHAR* AssetPath,
	bool bLoop,
	float PlayRate)
{
	FYUFSActionAnimationBinding& Binding = ActionAnimations.AddDefaulted_GetRef();
	Binding.Action = Action;
	Binding.Animation = TSoftObjectPtr<UAnimationAsset>(FSoftObjectPath(AssetPath));
	Binding.bLoop = bLoop;
	Binding.PlayRate = PlayRate;
}
