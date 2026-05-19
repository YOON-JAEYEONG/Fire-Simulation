// Fill out your copyright notice in the Description page of Project Settings.


#include "SmokeAwareNPC.h"

#include "YUFSBinaryManager.h"
#include "YUFSHeterogeneousVolume.h"
#include "Kismet/GameplayStatics.h"

// Sets default values
ASmokeAwareNPC::ASmokeAwareNPC()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

}

// Called when the game starts or when spawned
void ASmokeAwareNPC::BeginPlay()
{
	Super::BeginPlay();
	
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
			
			UE_LOG(LogTemp, Warning, TEXT("[NPC %s] 위치: %s | 매핑 인덱스 연기 농도: %d"), 
				*GetName(), *MyEyeLocation.ToString(), CurrentSmokeDensity);
			UE_LOG(LogTemp, Warning, TEXT("[NPC %s] 위치: %s | 매핑 인덱스 온도: %d"), 
				*GetName(), *MyEyeLocation.ToString(), CurrentTemperature);
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("NPC가 시뮬레이션 메쉬 영역 밖에 있습니다."));
		}
	}
}

// Called to bind functionality to input
//void ASmokeAwareNPC::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
//{
//	Super::SetupPlayerInputComponent(PlayerInputComponent);
//
//}

