#include "SimulationUIWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/GameplayStatics.h"
#include "Math/UnrealMathUtility.h"

void USimulationUIWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (StartButton)
    {
        StartButton->OnClicked.AddDynamic(this, &USimulationUIWidget::OnStartClicked);
    }

    if (PauseButton)
    {
        PauseButton->OnClicked.AddDynamic(this, &USimulationUIWidget::OnPauseClicked);
    }

    if (StopButton)
    {
        StopButton->OnClicked.AddDynamic(this, &USimulationUIWidget::OnStopClicked);
    }

    if (ResetButton)
    {
        ResetButton->OnClicked.AddDynamic(this, &USimulationUIWidget::OnResetClicked);
    }

    if (CameraOverviewButton)
    {
        CameraOverviewButton->OnClicked.AddDynamic(this, &USimulationUIWidget::OnCameraOverviewClicked);
    }

    if (CameraFireZoneButton)
    {
        CameraFireZoneButton->OnClicked.AddDynamic(this, &USimulationUIWidget::OnCameraFireZoneClicked);
    }

    UpdateSimulationStats();
}

void USimulationUIWidget::OnStartClicked()
{
    bIsSimulationRunning = true;
    UGameplayStatics::SetGamePaused(GetWorld(), false);
    UE_LOG(LogTemp, Warning, TEXT("Simulation Start"));
}

void USimulationUIWidget::OnPauseClicked()
{
    bIsSimulationRunning = false;
    UGameplayStatics::SetGamePaused(GetWorld(), true);
    UE_LOG(LogTemp, Warning, TEXT("Simulation Paused"));
}

void USimulationUIWidget::OnStopClicked()
{
    UE_LOG(LogTemp, Warning, TEXT("Simulation Stop"));
}

void USimulationUIWidget::OnResetClicked()
{
    if (GetWorld())
    {
        FString CurrentLevelName = UGameplayStatics::GetCurrentLevelName(GetWorld());
        UGameplayStatics::OpenLevel(GetWorld(), FName(*CurrentLevelName));
    }
}

void USimulationUIWidget::OnCameraOverviewClicked()
{
    UE_LOG(LogTemp, Warning, TEXT("Overview button clicked"));

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("World is null"));
        return;
    }

    APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
    if (!PC)
    {
        UE_LOG(LogTemp, Error, TEXT("PlayerController is null"));
        return;
    }

    TArray<AActor*> Cameras;
    UGameplayStatics::GetAllActorsWithTag(World, FName("OverviewCamera"), Cameras);

    UE_LOG(LogTemp, Warning, TEXT("OverviewCamera count: %d"), Cameras.Num());

    if (Cameras.Num() > 0 && Cameras[0])
    {
        PC->SetViewTargetWithBlend(Cameras[0], 0.5f);
        UE_LOG(LogTemp, Warning, TEXT("Switched to OverviewCamera"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("OverviewCamera not found"));
    }
}

void USimulationUIWidget::OnCameraFireZoneClicked()
{
    UE_LOG(LogTemp, Warning, TEXT("Fire Zone button clicked"));

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("World is null"));
        return;
    }

    APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
    if (!PC)
    {
        UE_LOG(LogTemp, Error, TEXT("PlayerController is null"));
        return;
    }

    TArray<AActor*> Cameras;
    UGameplayStatics::GetAllActorsWithTag(World, FName("FireZoneCamera"), Cameras);

    UE_LOG(LogTemp, Warning, TEXT("FireZoneCamera count: %d"), Cameras.Num());

    if (Cameras.Num() > 0 && Cameras[0])
    {
        PC->SetViewTargetWithBlend(Cameras[0], 0.5f);
        UE_LOG(LogTemp, Warning, TEXT("Switched to FireZoneCamera"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("FireZoneCamera not found"));
    }
}

void USimulationUIWidget::UpdateSimulationStats()
{
    if (NpcCountText)
    {
        NpcCountText->SetText(FText::FromString(TEXT("NPC: 20")));
    }

    if (EvacuatedCountText)
    {
        EvacuatedCountText->SetText(FText::FromString(TEXT("Evacuated: 0")));
    }

    if (SmokeLevelText)
    {
        SmokeLevelText->SetText(FText::FromString(TEXT("Smoke Level: 65")));
    }

    if (SimulationTimeText)
    {
        SimulationTimeText->SetText(FText::FromString(TEXT("00:00 / 01:00")));
		LastDisplayedSimulationSecond = 0;
    }
}
void USimulationUIWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    if (!bIsSimulationRunning)
    {
        return;
    }

    SimulationElapsedTime += InDeltaTime;

    if (SimulationElapsedTime > SimulationDuration)
    {
        SimulationElapsedTime = SimulationDuration;
    }

    int32 CurrentSeconds = FMath::FloorToInt(SimulationElapsedTime);
    if (CurrentSeconds == LastDisplayedSimulationSecond)
    {
        return;
    }

    LastDisplayedSimulationSecond = CurrentSeconds;
    int32 TotalSeconds = FMath::FloorToInt(SimulationDuration);

    FString TimeString = FString::Printf(
        TEXT("%02d:%02d / %02d:%02d"),
        CurrentSeconds / 60,
        CurrentSeconds % 60,
        TotalSeconds / 60,
        TotalSeconds % 60
    );

    if (SimulationTimeText)
    {
        SimulationTimeText->SetText(FText::FromString(TimeString));
    }
}
