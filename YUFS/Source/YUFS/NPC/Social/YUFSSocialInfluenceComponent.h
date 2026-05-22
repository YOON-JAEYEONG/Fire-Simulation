// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "YUFSSocialInfluenceComponent.generated.h"


UCLASS( ClassGroup=(YUFS), meta=(BlueprintSpawnableComponent) )
class YUFS_API UYUFSSocialInfluenceComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UYUFSSocialInfluenceComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:	
	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

public:
	void UpdateSocialContext();

	// Observation 빌드 시 읽어감
	float GetNearbyEvacuatingRatio() const;
	int32 GetNearbyNPCCount() const;
	FVector GetAverageEvacuationDestination() const;

	bool  ShouldHelpNearbyNPC()      const;
	float GetGroupSpeedMultiplier()  const; // 그룹 → 가장 느린 멤버 속도

	UPROPERTY(EditAnywhere) float SocialInfluenceRadius   = 500.f;
	UPROPERTY(EditAnywhere) float BystanderEffectStrength = 0.6f;
	// 논문(Aguirre): 대형 그룹은 대피 6.7분 지연
	UPROPERTY(EditAnywhere) float SocialDelayPerMember    = 1.2f;

private:
	TArray<ACharacter*> NearbyNPCs;
	int32 EvacuatingCount = 0;
	bool bCachedShouldHelpNearbyNPC = false;
	bool bHasNPCNeedingHelp = false;   // 실제 도움이 필요한 NPC 존재 여부 (Crawling/Incapacitated)
	int32 CachedNearbyNPCCount = -1;  // -1: 미초기화, 변화 감지용
		
};
