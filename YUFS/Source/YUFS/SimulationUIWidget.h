#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SimulationUIWidget.generated.h"

UCLASS()
class YUFS_API USimulationUIWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
    float SimulationElapsedTime = 0.0f;
    float SimulationDuration = 60.0f;
    bool bIsSimulationRunning = true;

protected:
    UFUNCTION()
    void OnStartClicked();

    UFUNCTION()
    void OnPauseClicked();

    UFUNCTION()
    void OnStopClicked();

    UFUNCTION()
    void OnResetClicked();

    UFUNCTION()
    void OnCameraOverviewClicked();

    UFUNCTION()
    void OnCameraFireZoneClicked();

    UFUNCTION()
    void UpdateSimulationStats();

protected:
    UPROPERTY(meta = (BindWidget))
    class UButton* StartButton;

    UPROPERTY(meta = (BindWidget))
    class UButton* PauseButton;

    UPROPERTY(meta = (BindWidget))
    class UButton* StopButton;

    UPROPERTY(meta = (BindWidget))
    class UButton* ResetButton;

    UPROPERTY(meta = (BindWidget))
    class UButton* CameraOverviewButton;

    UPROPERTY(meta = (BindWidget))
    class UButton* CameraFireZoneButton;

    UPROPERTY(meta = (BindWidget))
    class UTextBlock* SimulationTimeText;

    UPROPERTY(meta = (BindWidget))
    class UTextBlock* NpcCountText;

    UPROPERTY(meta = (BindWidget))
    class UTextBlock* EvacuatedCountText;

    UPROPERTY(meta = (BindWidget))
    class UTextBlock* SmokeLevelText;
};