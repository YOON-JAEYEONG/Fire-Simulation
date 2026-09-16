#include "UI/YUFSScenarioSetupWidget.h"

#include "Simulation/YUFSGameInstance.h"

void UYUFSScenarioSetupWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// AvailableMaps에 항목이 있고 WorkingConfig에 맵이 없으면 첫 맵을 기본 선택
	if (WorkingConfig.SimulationMap.IsNull() && AvailableMaps.Num() > 0)
	{
		SelectMapByIndex(0);
	}
}

UYUFSGameInstance* UYUFSScenarioSetupWidget::GetYUFSGameInstance() const
{
	return GetGameInstance() ? Cast<UYUFSGameInstance>(GetGameInstance()) : nullptr;
}

void UYUFSScenarioSetupWidget::InitializeWith(const FYUFSScenarioConfig& Config)
{
	WorkingConfig = Config;

	// 넘어온 맵이 AvailableMaps에 있으면 인덱스를 맞춰 둡니다.
	SelectedMapIndex = INDEX_NONE;
	for (int32 i = 0; i < AvailableMaps.Num(); ++i)
	{
		if (AvailableMaps[i].Map.ToSoftObjectPath() == Config.SimulationMap.ToSoftObjectPath())
		{
			SelectedMapIndex = i;
			break;
		}
	}

	OnConfigRefreshed();
}

void UYUFSScenarioSetupWidget::SelectMapByIndex(int32 Index)
{
	if (!AvailableMaps.IsValidIndex(Index))
	{
		return;
	}

	SelectedMapIndex = Index;
	WorkingConfig.SimulationMap = AvailableMaps[Index].Map;

	if (!AvailableMaps[Index].DisplayName.IsEmpty())
	{
		WorkingConfig.DisplayName = AvailableMaps[Index].DisplayName;
	}
}

void UYUFSScenarioSetupWidget::SetNPCCount(int32 Count)
{
	WorkingConfig.NPCCount = FMath::Max(0, Count);
}

void UYUFSScenarioSetupWidget::SetPolicyType(EYUFSScenarioPolicy Policy)
{
	WorkingConfig.PolicyType = Policy;
}

void UYUFSScenarioSetupWidget::SetPlacementPreset(EYUFSPlacementPreset Preset)
{
	WorkingConfig.PlacementPreset = Preset;
}

void UYUFSScenarioSetupWidget::SetFireStartDelay(float Seconds)
{
	WorkingConfig.FireStartDelaySeconds = FMath::Max(0.f, Seconds);
}

void UYUFSScenarioSetupWidget::SetAlarmEnabled(bool bEnabled, float DefaultOffset)
{
	WorkingConfig.AlarmTriggerOffsetSeconds = bEnabled ? FMath::Max(0.f, DefaultOffset) : -1.f;
}

void UYUFSScenarioSetupWidget::SetPreRecordedMsgEnabled(bool bEnabled, float DefaultOffset)
{
	WorkingConfig.PreRecordedMsgOffsetSeconds = bEnabled ? FMath::Max(0.f, DefaultOffset) : -1.f;
}

void UYUFSScenarioSetupWidget::SetLiveAnnouncementEnabled(bool bEnabled, float DefaultOffset)
{
	WorkingConfig.LiveAnnouncementOffsetSeconds = bEnabled ? FMath::Max(0.f, DefaultOffset) : -1.f;
}

void UYUFSScenarioSetupWidget::SetStaffGuidanceEnabled(bool bEnabled, float DefaultOffset)
{
	WorkingConfig.StaffGuidanceOffsetSeconds = bEnabled ? FMath::Max(0.f, DefaultOffset) : -1.f;
}

bool UYUFSScenarioSetupWidget::ValidateConfig(FText& OutError) const
{
	return WorkingConfig.IsValid(OutError);
}

void UYUFSScenarioSetupWidget::ConfirmAndLaunch()
{
	FText Error;
	if (!WorkingConfig.IsValid(Error))
	{
		UE_LOG(LogTemp, Warning, TEXT("[YUFS] 시나리오 검증 실패: %s"), *Error.ToString());
		OnValidationFailed(Error);
		return;
	}

	if (UYUFSGameInstance* GI = GetYUFSGameInstance())
	{
		GI->LaunchScenario(WorkingConfig);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[YUFS] UYUFSGameInstance를 찾을 수 없어 시나리오를 시작하지 못했습니다."));
	}
}
