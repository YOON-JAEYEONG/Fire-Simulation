// Fill out your copyright notice in the Description page of Project Settings.

#include "NPC/YUFSEvacuationNPC.h"

#include "Behavior/YUFSBehaviorStateMachine.h"
#include "Communication/YUFSCommTypes.h"
#include "Communication/YUFSEmergencyCommSystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/YUFSObservation.h"
#include "Core/YUFSRewardCalculator.h"
#include "Debug/YUFSNPCDebugComponent.h"
#include "EngineUtils.h"
#include "Fire/YUFSBinaryManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Level/YUFSLevelDataManager.h"
#include "Navigation/YUFSSmokeAwareNavigator.h"
#include "Perception/YUFSNPCPerceptionComponent.h"
#include "Social/YUFSSocialInfluenceComponent.h"

AYUFSEvacuationNPC::AYUFSEvacuationNPC()
{
	PrimaryActorTick.bCanEverTick = true;

	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	PerceptionComp = CreateDefaultSubobject<UYUFSNPCPerceptionComponent>(TEXT("YUFSNPCPerceptionComponent"));
	BehaviorSM = CreateDefaultSubobject<UYUFSBehaviorStateMachine>(TEXT("YUFSBehaviorStateMachine"));
	Navigator = CreateDefaultSubobject<UYUFSSmokeAwareNavigator>(TEXT("YUFSSmokeAwareNavigator"));
	SocialComp = CreateDefaultSubobject<UYUFSSocialInfluenceComponent>(TEXT("YUFSSocialInfluenceComponent"));
	DebugComp = CreateDefaultSubobject<UYUFSNPCDebugComponent>(TEXT("YUFSNPCDebugComponent"));

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	if (GetCharacterMovement())
	{
		GetCharacterMovement()->bOrientRotationToMovement = true;
		GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f);
	}

	if (UCapsuleComponent* CapsuleComp = GetCapsuleComponent())
	{
		CapsuleComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	}

	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	}
}

void AYUFSEvacuationNPC::BeginPlay()
{
	Super::BeginPlay();

	AActor* CommActor = UGameplayStatics::GetActorOfClass(GetWorld(), AYUFSEmergencyCommSystem::StaticClass());
	if (AYUFSEmergencyCommSystem* CommSystem = Cast<AYUFSEmergencyCommSystem>(CommActor))
	{
		CommSystem->OnEmergencyComm.AddDynamic(this, &AYUFSEvacuationNPC::OnCommReceived);
	}

	for (TActorIterator<AYUFSBinaryManager> It(GetWorld()); It; ++It)
	{
		BinaryManager = *It;
		break;
	}

	for (TActorIterator<AYUFSLevelDataManager> It(GetWorld()); It; ++It)
	{
		LevelDataMgr = *It;
		break;
	}

	DecisionPolicy = FYUFSPolicyFactory::Create(PolicyType);
}

void AYUFSEvacuationNPC::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	if (!DecisionPolicy)
	{
		return;
	}

	UpdateLookingAround(DeltaTime);

	const int32 CurrentFrame = GetCurrentSimFrame();
	if (PerceptionComp)
	{
		PerceptionComp->UpdatePerception(CurrentFrame);
	}

	if (SocialComp)
	{
		SocialComp->UpdateSocialContext();
	}

	FYUFSNPCObservation CurrentObs{};
	BuildObservation(CurrentObs);

	if (BehaviorSM)
	{
		BehaviorSM->TickStateMachine(DeltaTime, CurrentObs);
	}

	BuildObservation(CurrentObs);
	const EYUFSAction SelectedAction = DecisionPolicy->SelectAction(CurrentObs);

	ExecuteAction(SelectedAction);

	if (DecisionPolicy->IsLearningMode())
	{
		const float Reward = CalculateCurrentReward(CurrentObs, SelectedAction);
		DecisionPolicy->OnTransition(PrevObservation, Reward, false);
	}

	PrevObservation = CurrentObs;
	LastAction = SelectedAction;

	if (GetCharacterMovement())
	{
		float TargetSpeed = 250.f;
		bool bIsMovingAction = false;
		
		switch (SelectedAction)
		{
		case EYUFSAction::EvacuateToNearestExit:
		case EYUFSAction::EvacuateToFamiliarExit:
		case EYUFSAction::MoveToShelter:
		case EYUFSAction::FollowCrowd:
			TargetSpeed = 600.f;
			bIsMovingAction = true;
			break;

		case EYUFSAction::SeekInformation:
			TargetSpeed = 0.f;
			break;

		case EYUFSAction::Cough:
			TargetSpeed = 100.f;
			break;

		default:
			break;
		}

		GetCharacterMovement()->MaxWalkSpeed = TargetSpeed;

		if (bIsMovingAction)
		{
			if (GetVelocity().Size2D() < 10.f)
			{
				StuckTimer += DeltaTime;
				if (StuckTimer > 2.0f)
				{
					if (Navigator)
					{
						Navigator->ClearPath();
					}
					StuckTimer = 0.0f;
				}
			}
			else
			{
				StuckTimer = 0.0f;
			}
		}
		else
		{
			StuckTimer = 0.0f;
		}
	}

	if (GEngine && Navigator)
	{
		const UEnum* StateEnum = StaticEnum<EYUFSBehaviorState>();
		const UEnum* ActionEnum = StaticEnum<EYUFSAction>();
		
		const FString StateName = StateEnum
			? StateEnum->GetNameStringByValue((int64)CurrentObs.CurrentState)
			: FString::FromInt((int32)CurrentObs.CurrentState);
		const FString ActionName = ActionEnum
			? ActionEnum->GetNameStringByValue((int64)SelectedAction)
			: FString::FromInt((int32)SelectedAction);

		const FVector SafeExit = LevelDataMgr
			? LevelDataMgr->GetNearestSafeExit(GetActorLocation(), true, GetCurrentSimFrame())
			: FVector::ZeroVector;
		const FString DebugStr = FString::Printf(
			TEXT("[NPC] State: %s | Action: %s | Dest: %s | SafeExit: %s"),
			*StateName,
			*ActionName,
			*Navigator->GetCurrentDestination().ToString(),
			*SafeExit.ToString());
		GEngine->AddOnScreenDebugMessage(-1, 0.0f, FColor::Yellow, DebugStr);
	}
}

void AYUFSEvacuationNPC::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
}

void AYUFSEvacuationNPC::OnCommReceived(EYUFSCommType CommType, FVector SourceLocation, float EffectiveRadius, FVector GuidanceTarget)
{
	const float DistSq = FVector::DistSquared(GetActorLocation(), SourceLocation);
	if (DistSq > EffectiveRadius * EffectiveRadius)
	{
		return;
	}

	switch (CommType)
	{
	case EYUFSCommType::AlarmOnly:
		bAlarmSounding = true;
		if (BehaviorSM)
		{
			BehaviorSM->OnAlarmReceived();
		}
		break;

	case EYUFSCommType::PreRecordedMessage:
		bReceivedPreRecordedMsg = true;
		break;

	case EYUFSCommType::LiveAnnouncement:
		bReceivedLiveAnnouncement = true;
		if (BehaviorSM)
		{
			BehaviorSM->OnLiveAnnouncementReceived();
		}
		break;

	case EYUFSCommType::StaffGuidance:
		bReceivedStaffGuidance = true;
		StaffGuidedExitLocation = GuidanceTarget;
		if (BehaviorSM)
		{
			BehaviorSM->OnStaffGuidanceReceived();
		}
		break;
	}
}

int32 AYUFSEvacuationNPC::GetCurrentSimFrame() const
{
	return BinaryManager ? BinaryManager->GetCurrentFrame() : 0;
}

void AYUFSEvacuationNPC::BuildObservation(FYUFSNPCObservation& Out) const
{
	Out = FYUFSNPCObservation{};

	if (!PerceptionComp || !BehaviorSM || !SocialComp || !Navigator || !LevelDataMgr)
	{
		return;
	}

	Out.SmokeDensityAtSelf = PerceptionComp->GetSmokeDensity();
	Out.TemperatureAtSelf = PerceptionComp->GetTemperature();
	Out.SmokeInFrontNormalized = PerceptionComp->GetSmokeInFrontNormalized();
	Out.SmokeAboveNormalized = PerceptionComp->GetSmokeAboveNormalized();
	Out.RiskLevel = PerceptionComp->GetRiskLevel();
	Out.CurrentState = BehaviorSM->GetCurrentState();
	Out.RiskPerception = BehaviorSM->GetRiskPerception();
	Out.StressLevel = PerceptionComp->GetRiskLevel();
	Out.StaffGuidedExitLocation = StaffGuidedExitLocation;
	Out.bAlarmSounding = bAlarmSounding;
	Out.bReceivedPreRecordedMsg = bReceivedPreRecordedMsg;
	Out.bReceivedLiveAnnouncement = bReceivedLiveAnnouncement;
	Out.bReceivedStaffGuidance = bReceivedStaffGuidance;
	Out.NearbyEvacuatingRatio = SocialComp->GetNearbyEvacuatingRatio();
	Out.NearbyNPCCount = SocialComp->GetNearbyNPCCount();
	Out.GroupSize = SocialComp->GetNearbyNPCCount() + 1;
	Out.bNearbyNPCNeedsHelp = SocialComp->ShouldHelpNearbyNPC();

	const FVector CurrentPos = GetActorLocation();
	const int32 CurrentFrame = GetCurrentSimFrame();
	const FVector NearestSafeExit = LevelDataMgr->GetNearestSafeExit(CurrentPos, true, CurrentFrame);
	const FVector FamiliarExit = LevelDataMgr->GetFamiliarExit(CurrentPos);
	const FVector NearestShelter = LevelDataMgr->GetNearestAvailableShelter(CurrentPos);

	Out.DistToNearestExit = FVector::Distance(CurrentPos, NearestSafeExit);
	Out.DistToFamiliarExit = FVector::Distance(CurrentPos, FamiliarExit);
	Out.DistToNearestShelter = FVector::Distance(CurrentPos, NearestShelter);
	Out.DirToNearestExit = (NearestSafeExit - CurrentPos).GetSafeNormal();
	Out.SimTimeNormalized = FMath::Clamp(static_cast<float>(CurrentFrame) / 8000.f, 0.f, 1.f);
	Out.bNearestExitSmokeFree = !LevelDataMgr->IsLocationDangerous(NearestSafeExit, CurrentFrame);
}

void AYUFSEvacuationNPC::ExecuteAction(EYUFSAction Action)
{
	if (!Navigator || !LevelDataMgr)
	{
		return;
	}

	FVector TargetDestination = GetActorLocation();
	bool bShouldMove = false;

	switch (Action)
	{
	case EYUFSAction::EvacuateToNearestExit:
		TargetDestination = LevelDataMgr->GetNearestSafeExit(GetActorLocation(), true, GetCurrentSimFrame());
		bShouldMove = true;
		break;

	case EYUFSAction::EvacuateToFamiliarExit:
		TargetDestination = LevelDataMgr->GetFamiliarExit(GetActorLocation());
		bShouldMove = true;
		break;

	case EYUFSAction::MoveToShelter:
		TargetDestination = LevelDataMgr->GetNearestAvailableShelter(GetActorLocation());
		bShouldMove = true;
		break;

	case EYUFSAction::FollowCrowd:
		if (SocialComp)
		{
			TargetDestination = SocialComp->GetAverageEvacuationDestination();
			if (TargetDestination == FVector::ZeroVector)
			{
				TargetDestination = LevelDataMgr->GetNearestSafeExit(GetActorLocation(), true, GetCurrentSimFrame());
			}
		}
		bShouldMove = true;
		break;

	case EYUFSAction::GatherBelongings:
	case EYUFSAction::SeekInformation:
	case EYUFSAction::Cough:
	case EYUFSAction::AlertNearbyOccupants:
	case EYUFSAction::HelpOther:
	case EYUFSAction::Idle:
	default:
		break;
	}

	if (!bShouldMove)
	{
		return;
	}

	Navigator->CheckAndReroute(GetCurrentSimFrame());
	
	const FVector CurrentNavDest = Navigator->GetCurrentDestination();
	if (CurrentNavDest == FVector::ZeroVector || FVector::Distance(TargetDestination, CurrentNavDest) > 500.f)
	{
		Navigator->RequestPathAsync(TargetDestination, GetCurrentSimFrame());
	}
	
	Navigator->UpdateWaypoint(GetActorLocation(), 100.f);

	FVector Direction = (Navigator->GetNextWaypoint() - GetActorLocation()).GetSafeNormal();
	Direction.Z = 0.f;
	Direction.Normalize();

	const float SpeedMultiplier = SocialComp ? SocialComp->GetGroupSpeedMultiplier() : 1.0f;
	AddMovementInput(Direction, SpeedMultiplier);
}

float AYUFSEvacuationNPC::CalculateCurrentReward(const FYUFSNPCObservation& Obs, EYUFSAction Action) const
{
	bool bReachedExit = false;
	if (LevelDataMgr)
	{
		const FVector NearestExit = LevelDataMgr->GetNearestSafeExit(GetActorLocation(), false, GetCurrentSimFrame());
		if (FVector::Distance(GetActorLocation(), NearestExit) < 200.f)
		{
			bReachedExit = true;
		}
	}
	
	return YUFSRewardCalculator::Calculate(PrevObservation, Action, Obs, bReachedExit);
}

void AYUFSEvacuationNPC::UpdateLookingAround(float DeltaTime)
{
	const bool bShouldLookAround = LastAction == EYUFSAction::SeekInformation && BehaviorSM && BehaviorSM->Config;
	if (!bShouldLookAround)
	{
		LookAroundElapsedTime = 0.f;
		bWasLookingAroundLastFrame = false;
		return;
	}

	if (!bWasLookingAroundLastFrame)
	{
		LookAroundAnchorYaw = GetActorRotation().Yaw;
		LookAroundElapsedTime = 0.f;
		bWasLookingAroundLastFrame = true;
	}

	LookAroundElapsedTime += DeltaTime;

	const float Oscillation = FMath::Sin(LookAroundElapsedTime * 2.f * PI * BehaviorSM->Config->LookAroundFrequencyHz);
	FRotator LookingRotation = GetActorRotation();
	LookingRotation.Yaw = LookAroundAnchorYaw + (Oscillation * BehaviorSM->Config->LookAroundYawAmplitudeDegrees);
	SetActorRotation(LookingRotation);
}
