#include "UI/YUFSSettingsWidget.h"

#include "GameFramework/GameUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Simulation/YUFSGameInstance.h"

namespace
{
	EWindowMode::Type ToEngineWindowMode(EYUFSWindowMode Mode)
	{
		switch (Mode)
		{
		case EYUFSWindowMode::Fullscreen:         return EWindowMode::Fullscreen;
		case EYUFSWindowMode::WindowedFullscreen: return EWindowMode::WindowedFullscreen;
		case EYUFSWindowMode::Windowed:           return EWindowMode::Windowed;
		default:                                  return EWindowMode::Windowed;
		}
	}

	EYUFSWindowMode FromEngineWindowMode(EWindowMode::Type Mode)
	{
		switch (Mode)
		{
		case EWindowMode::Fullscreen:         return EYUFSWindowMode::Fullscreen;
		case EWindowMode::WindowedFullscreen: return EYUFSWindowMode::WindowedFullscreen;
		default:                              return EYUFSWindowMode::Windowed;
		}
	}
}

void UYUFSSettingsWidget::NativeConstruct()
{
	// Super::NativeConstruct()가 내부에서 BP의 Event Construct(ReceiveConstruct)를 바로
	// 호출하므로, BP가 참조하는 값은 Super 호출 전에 미리 채워둬야 함.
	RefreshAvailableResolutions();
	Super::NativeConstruct();
}

UYUFSGameInstance* UYUFSSettingsWidget::GetYUFSGameInstance() const
{
	return GetGameInstance() ? Cast<UYUFSGameInstance>(GetGameInstance()) : nullptr;
}

void UYUFSSettingsWidget::RefreshAvailableResolutions()
{
	AvailableResolutions.Empty();
	UKismetSystemLibrary::GetSupportedFullscreenResolutions(AvailableResolutions);

	if (AvailableResolutions.Num() == 0)
	{
		// 지원 목록을 못 가져온 환경을 대비한 대표 해상도 목록
		AvailableResolutions = {
			FIntPoint(1280, 720),
			FIntPoint(1600, 900),
			FIntPoint(1920, 1080),
			FIntPoint(2560, 1440),
		};
	}

	SelectedResolutionIndex = INDEX_NONE;
	if (const UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings())
	{
		SelectedResolutionIndex = AvailableResolutions.IndexOfByKey(Settings->GetScreenResolution());
	}
}

void UYUFSSettingsWidget::SelectResolutionByIndex(int32 Index)
{
	if (!AvailableResolutions.IsValidIndex(Index))
	{
		return;
	}

	SelectedResolutionIndex = Index;
	if (UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings())
	{
		Settings->SetScreenResolution(AvailableResolutions[Index]);
	}
}

FText UYUFSSettingsWidget::GetResolutionText(int32 Index) const
{
	if (!AvailableResolutions.IsValidIndex(Index))
	{
		return FText::GetEmpty();
	}

	const FIntPoint& R = AvailableResolutions[Index];
	return FText::FromString(FString::Printf(TEXT("%d x %d"), R.X, R.Y));
}

void UYUFSSettingsWidget::SetWindowMode(EYUFSWindowMode Mode)
{
	if (UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings())
	{
		Settings->SetFullscreenMode(ToEngineWindowMode(Mode));
	}
}

EYUFSWindowMode UYUFSSettingsWidget::GetWindowMode() const
{
	const UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings();
	return Settings ? FromEngineWindowMode(Settings->GetFullscreenMode()) : EYUFSWindowMode::Windowed;
}

void UYUFSSettingsWidget::SetVSyncEnabled(bool bEnabled)
{
	if (UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings())
	{
		Settings->SetVSyncEnabled(bEnabled);
	}
}

bool UYUFSSettingsWidget::IsVSyncEnabled() const
{
	const UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings();
	return Settings && Settings->IsVSyncEnabled();
}

void UYUFSSettingsWidget::SetGraphicsQuality(EYUFSGraphicsQuality Quality)
{
	if (UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings())
	{
		Settings->SetOverallScalabilityLevel(static_cast<int32>(Quality));
	}
}

EYUFSGraphicsQuality UYUFSSettingsWidget::GetGraphicsQuality() const
{
	const UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings();
	if (!Settings)
	{
		return EYUFSGraphicsQuality::Medium;
	}

	// 항목별로 다른 값이 섞여 있으면 -1(Custom)을 반환하므로 Medium으로 대표합니다.
	const int32 Level = FMath::Clamp(Settings->GetOverallScalabilityLevel(), 0, 3);
	return static_cast<EYUFSGraphicsQuality>(Level);
}

void UYUFSSettingsWidget::ApplyAndSaveGraphicsSettings()
{
	if (UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings())
	{
		Settings->ApplySettings(false);
		Settings->SaveSettings();
	}
}

void UYUFSSettingsWidget::RevertGraphicsSettings()
{
	if (UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings())
	{
		Settings->LoadSettings(true);
		Settings->ApplySettings(false);
	}

	RefreshAvailableResolutions();
}

void UYUFSSettingsWidget::SetNPCDebugOverlayEnabled(bool bEnabled)
{
	if (UYUFSGameInstance* GI = GetYUFSGameInstance())
	{
		GI->bNPCDebugOverlayEnabled = bEnabled;
	}
}

bool UYUFSSettingsWidget::IsNPCDebugOverlayEnabled() const
{
	const UYUFSGameInstance* GI = GetYUFSGameInstance();
	return GI && GI->bNPCDebugOverlayEnabled;
}
