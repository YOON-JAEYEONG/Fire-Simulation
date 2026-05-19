// Fill out your copyright notice in the Description page of Project Settings.


#include "SmokeAwareNPC.h"

#include "YUFSBinaryManager.h"
#include "YUFSHeterogeneousVolume.h"
#include "Kismet/GameplayStatics.h"
#include "FireEvacAIController.h"

#include "Components/TextRenderComponent.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"

// Sets default values
ASmokeAwareNPC::ASmokeAwareNPC()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	AIControllerClass = AFireEvacAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	StateText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("StateText"));
	StateText->SetupAttachment(RootComponent);

	StateText->SetRelativeLocation(FVector(0.0f, 0.0f, 120.0f));
	StateText->SetHorizontalAlignment(EHTA_Center);
	StateText->SetWorldSize(35.0f);
	StateText->SetText(FText::FromString(TEXT("Idle")));
	StateText->SetTextRenderColor(FColor::White);
}

// Called when the game starts or when spawned
void ASmokeAwareNPC::BeginPlay()
{
	Super::BeginPlay();
	
	if (bRandomizeTraits)
	{
		RandomizeTraits();
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[NPC Trait] %s | Calmness=%.2f FearSensitivity=%.2f Altruism=%.2f MoveAbility=%.2f ExitSelectMode=%d"),
		*GetName(),
		Calmness,
		FearSensitivity,
		Altruism,
		MoveAbility,
		ExitSelectMode
	);

	FireManager = Cast<AYUFSBinaryManager>(UGameplayStatics::GetActorOfClass(GetWorld(), AYUFSBinaryManager::StaticClass()));
    
	if (!FireManager)
	{
		UE_LOG(LogTemp, Error, TEXT("맵에서 AMyBinaryManager를 찾을 수 없습니다!"));
	}
}

// Called every frame
void ASmokeAwareNPC::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdateStateVisual();
	if (FireManager)
	{
		uint8 CurrentSmokeDensity = 0;
		uint8 CurrentTemperature = 0;
		
		FVector MyEyeLocation = GetPawnViewLocation();
		
		int32 CurrentFrame = 0;
		AYUFSHeterogeneousVolume* HetVolume = FireManager->GetHeterogeneousVolume();

		
		
		if (IsValid(HetVolume))
		{
			CurrentFrame = HetVolume->GetFrame();
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("맵에 AMyHeterogeneousVolume 액터가 없습니다!"));
		}

		bool bIsInside = FireManager->GetSmokeDensityAtLocation(MyEyeLocation, CurrentFrame, CurrentSmokeDensity);
		if (bIsInside)
		{
			FireManager->GetTemperatureAtLocation(MyEyeLocation, CurrentFrame, CurrentTemperature);
			
			//UE_LOG(LogTemp, Warning, TEXT("[NPC %s] 위치: %s | 매핑 인덱스 연기 농도: %d"), 
			//	*GetName(), *MyEyeLocation.ToString(), CurrentSmokeDensity);
			//UE_LOG(LogTemp, Warning, TEXT("[NPC %s] 위치: %s | 매핑 인덱스 온도: %d"), 
			//	*GetName(), *MyEyeLocation.ToString(), CurrentTemperature);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("NPC가 시뮬레이션 메쉬 영역 밖에 있습니다.123"));
		}
	}
}
void ASmokeAwareNPC::RandomizeTraits()
{
	float SafeMinCalmness = FMath::Min(MinCalmness, MaxCalmness);
	float SafeMaxCalmness = FMath::Max(MinCalmness, MaxCalmness);

	float SafeMinFearSensitivity = FMath::Min(MinFearSensitivity, MaxFearSensitivity);
	float SafeMaxFearSensitivity = FMath::Max(MinFearSensitivity, MaxFearSensitivity);

	float SafeMinAltruism = FMath::Min(MinAltruism, MaxAltruism);
	float SafeMaxAltruism = FMath::Max(MinAltruism, MaxAltruism);

	float SafeMinMoveAbility = FMath::Min(MinMoveAbility, MaxMoveAbility);
	float SafeMaxMoveAbility = FMath::Max(MinMoveAbility, MaxMoveAbility);

	Calmness = FMath::FRandRange(SafeMinCalmness, SafeMaxCalmness);
	FearSensitivity = FMath::FRandRange(SafeMinFearSensitivity, SafeMaxFearSensitivity);
	Altruism = FMath::FRandRange(SafeMinAltruism, SafeMaxAltruism);
	MoveAbility = FMath::FRandRange(SafeMinMoveAbility, SafeMaxMoveAbility);

	Calmness = FMath::Clamp(Calmness, 0.0f, 1.0f);
	FearSensitivity = FMath::Clamp(FearSensitivity, 0.1f, 3.0f);
	Altruism = FMath::Clamp(Altruism, 0.0f, 1.0f);
	MoveAbility = FMath::Clamp(MoveAbility, 0.1f, 2.0f);

	ExitSelectMode = FMath::RandRange(0, 2);

	bIsInjured = false;
	bNeedsHelp = false;
	bIsHelping = false;

	UE_LOG(LogTemp, Warning,
		TEXT("[NPC Trait Randomized] %s | Calmness=%.2f FearSensitivity=%.2f Altruism=%.2f MoveAbility=%.2f ExitSelectMode=%d"),
		*GetName(),
		Calmness,
		FearSensitivity,
		Altruism,
		MoveAbility,
		ExitSelectMode
	);
}

void ASmokeAwareNPC::UpdateStateVisual()
{
	if (!StateText)
	{
		return;
	}

	StateText->SetVisibility(bShowStateText);

	if (!bShowStateText)
	{
		return;
	}

	FString StateName = TEXT("Wander");
	FColor StateColor = FColor::White;

	bool bAware = false;
	bool bPanic = false;
	bool bCanHelp = false;
	bool bHelping = bIsHelping;
	bool bInjured = bIsInjured;
	bool bNeedHelp = bNeedsHelp;

	AAIController* AIController = Cast<AAIController>(GetController());

	if (AIController)
	{
		UBlackboardComponent* Blackboard = AIController->GetBlackboardComponent();

		if (Blackboard)
		{
			bAware = Blackboard->GetValueAsBool(TEXT("IsAwareOfFire"));
			bPanic = Blackboard->GetValueAsBool(TEXT("IsPanic"));
			bCanHelp = Blackboard->GetValueAsBool(TEXT("CanHelpOthers"));
			bHelping = Blackboard->GetValueAsBool(TEXT("IsHelping"));
			bInjured = Blackboard->GetValueAsBool(TEXT("IsInjured"));
			bNeedHelp = Blackboard->GetValueAsBool(TEXT("NeedsHelp"));
		}
	}

	if (bHelping)
	{
		StateName = TEXT("Helping");
		StateColor = FColor::Cyan;
	}
	else if (bNeedHelp)
	{
		StateName = TEXT("Need Help");
		StateColor = FColor::Magenta;
	}
	else if (bInjured)
	{
		StateName = TEXT("Injured");
		StateColor = FColor::Purple;
	}
	else if (bPanic)
	{
		StateName = TEXT("Panic");
		StateColor = FColor::Red;
	}
	else if (bAware)
	{
		StateName = TEXT("Escape");
		StateColor = FColor::Orange;
	}
	else if (bCanHelp)
	{
		StateName = TEXT("Can Help");
		StateColor = FColor::Green;
	}
	else
	{
		StateName = TEXT("Wander");
		StateColor = FColor::White;
	}

	StateText->SetText(FText::FromString(StateName));
	StateText->SetTextRenderColor(StateColor);

	// 카메라를 향하게 하고 싶으면 일단 Z축만 회전
	if (GetWorld())
	{
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		if (PC && PC->PlayerCameraManager)
		{
			FVector CameraLocation = PC->PlayerCameraManager->GetCameraLocation();
			FVector TextLocation = StateText->GetComponentLocation();

			FRotator LookAtRotation = (CameraLocation - TextLocation).Rotation();
			StateText->SetWorldRotation(FRotator(0.0f, LookAtRotation.Yaw + 180.0f, 0.0f));
		}
	}
}

// Called to bind functionality to input
//void ASmokeAwareNPC::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
//{
//	Super::SetupPlayerInputComponent(PlayerInputComponent);
//
//}

