// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "SmokeAwareNPC.generated.h"

class AYUFSBinaryManager;
class UTextRenderComponent;

UCLASS()
class YUFS_API ASmokeAwareNPC : public ACharacter
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	ASmokeAwareNPC();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	UPROPERTY()
	AYUFSBinaryManager* FireManager;
public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Debug")
	UTextRenderComponent* StateText;

	UFUNCTION(BlueprintCallable, Category = "NPC Debug")
	void UpdateStateVisual();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Debug")
	bool bShowStateText = true;

	// NPC 성격 / 신체 / 행동 변수
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Trait")
	float Calmness = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Trait")
	float FearSensitivity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Trait")
	float Altruism = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Trait")
	float MoveAbility = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Trait")
	int32 ExitSelectMode = 0;
	// 0 = 가까운 출구, 1 = 랜덤 출구, 2 = 연기 적은 출구

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC State")
	bool bIsInjured = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC State")
	bool bNeedsHelp = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC State")
	bool bIsHelping = false;


	// 랜덤 설정 여부
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Random")
	bool bRandomizeTraits = false;


	// 랜덤 범위
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Random")
	float MinCalmness = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Random")
	float MaxCalmness = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Random")
	float MinFearSensitivity = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Random")
	float MaxFearSensitivity = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Random")
	float MinAltruism = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Random")
	float MaxAltruism = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Random")
	float MinMoveAbility = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Random")
	float MaxMoveAbility = 1.2f;


	UFUNCTION(BlueprintCallable, Category = "NPC Trait")
	void RandomizeTraits();
	// Called to bind functionality to input
	//virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

};
