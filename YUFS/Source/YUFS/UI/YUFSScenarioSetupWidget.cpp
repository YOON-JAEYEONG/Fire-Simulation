#include "UI/YUFSScenarioSetupWidget.h"

#include "Components/ComboBoxString.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Simulation/YUFSGameInstance.h"

#define LOCTEXT_NAMESPACE "YUFSScenarioSetupWidget"

void UYUFSScenarioSetupWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (MapCombo)
	{
		MapCombo->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::HandleMapSelectionChanged);
	}

	// AvailableMaps에 항목이 있고 WorkingConfig에 맵이 없으면 첫 맵을 기본 선택
	if (WorkingConfig.SimulationMap.IsNull() && AvailableMaps.Num() > 0)
	{
		SelectMapByIndex(0);
	}

	RefreshMapOptions();
}

void UYUFSScenarioSetupWidget::NativeDestruct()
{
	if (MapCombo)
	{
		MapCombo->OnSelectionChanged.RemoveDynamic(this, &ThisClass::HandleMapSelectionChanged);
	}

	Super::NativeDestruct();
}

UYUFSGameInstance* UYUFSScenarioSetupWidget::GetYUFSGameInstance() const
{
	return GetGameInstance() ? Cast<UYUFSGameInstance>(GetGameInstance()) : nullptr;
}

void UYUFSScenarioSetupWidget::InitializeWith(const FYUFSScenarioConfig& Config)
{
	WorkingConfig = Config;

	RefreshMapOptions();
	OnConfigRefreshed();
	// BP에서 다른 컨트롤을 갱신한 뒤에도 맵 표시를 최종 설정과 맞춥니다.
	RefreshMapOptions();
}

void UYUFSScenarioSetupWidget::RefreshMapOptions()
{
	if (bUpdatingMapCombo)
	{
		return;
	}

	// 설정에 없는 맵을 임의로 첫 맵으로 바꾸지 않습니다.
	SelectedMapIndex = INDEX_NONE;
	if (!WorkingConfig.SimulationMap.IsNull())
	{
		for (int32 i = 0; i < AvailableMaps.Num(); ++i)
		{
			if (AvailableMaps[i].Map.ToSoftObjectPath() == WorkingConfig.SimulationMap.ToSoftObjectPath())
			{
				SelectedMapIndex = i;
				break;
			}
		}
	}

	if (MapCombo)
	{
		TGuardValue<bool> UpdatingGuard(bUpdatingMapCombo, true);
		TArray<FString> Options;
		TSet<FString> UsedLabels;
		bool bOptionsChanged = MapCombo->GetOptionCount() != AvailableMaps.Num();
		for (int32 i = 0; i < AvailableMaps.Num(); ++i)
		{
			const FString BaseLabel = AvailableMaps[i].DisplayName.IsEmpty()
				? FText::Format(LOCTEXT("UnnamedMap", "맵 {0}"), FText::AsNumber(i + 1)).ToString()
				: AvailableMaps[i].DisplayName.ToString();
			FString Label = BaseLabel;
			// ComboBoxString은 문자열로 선택을 구분하므로 중복 이름도 고유하게 만듭니다.
			for (int32 Suffix = 2; UsedLabels.Contains(Label); ++Suffix)
			{
				Label = FString::Printf(TEXT("%s (%d)"), *BaseLabel, Suffix);
			}
			UsedLabels.Add(Label);
			Options.Add(Label);
			bOptionsChanged |= MapCombo->GetOptionAtIndex(i) != Label;
		}

		if (bOptionsChanged)
		{
			MapCombo->ClearOptions();
			for (const FString& Option : Options)
			{
				MapCombo->AddOption(Option);
			}
		}
	}

	SynchronizeMapSelection();
	UpdateMapPreview();
}

void UYUFSScenarioSetupWidget::SynchronizeMapSelection()
{
	if (MapCombo && MapCombo->GetSelectedIndex() != SelectedMapIndex)
	{
		TGuardValue<bool> UpdatingGuard(bUpdatingMapCombo, true);
		if (AvailableMaps.IsValidIndex(SelectedMapIndex))
		{
			MapCombo->SetSelectedIndex(SelectedMapIndex);
		}
		else
		{
			MapCombo->ClearSelection();
		}
	}
}

void UYUFSScenarioSetupWidget::HandleMapSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (bUpdatingMapCombo || !MapCombo)
	{
		return;
	}

	const int32 Index = MapCombo->GetSelectedIndex();
	if (AvailableMaps.IsValidIndex(Index))
	{
		SelectMapByIndex(Index);
	}
	else
	{
		SelectedMapIndex = INDEX_NONE;
		WorkingConfig.SimulationMap.Reset();
		WorkingConfig.DisplayName = FText::GetEmpty();
		UpdateMapPreview();
	}
}

void UYUFSScenarioSetupWidget::UpdateMapPreview()
{
	const FYUFSScenarioMapOption* SelectedMap = AvailableMaps.IsValidIndex(SelectedMapIndex)
		? &AvailableMaps[SelectedMapIndex] : nullptr;
	UTexture2D* Thumbnail = SelectedMap ? SelectedMap->Thumbnail.Get() : nullptr;
	const bool bHasThumbnail = IsValid(Thumbnail);

	if (Image_MapThumbnail)
	{
		// Match Size는 ScaleBox에서 원본 텍스처의 종횡비를 유지하는 데 사용합니다.
		Image_MapThumbnail->SetBrushFromTexture(bHasThumbnail ? Thumbnail : nullptr, true);
		Image_MapThumbnail->SetVisibility(bHasThumbnail
			? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}

	if (Text_MapPreviewHint)
	{
		Text_MapPreviewHint->SetText(SelectedMap
			? LOCTEXT("NoMapThumbnail", "미리보기 이미지가 없습니다")
			: LOCTEXT("SelectMap", "맵을 선택해 주세요"));
		Text_MapPreviewHint->SetVisibility(bHasThumbnail
			? ESlateVisibility::Hidden : ESlateVisibility::HitTestInvisible);
	}
}

void UYUFSScenarioSetupWidget::SelectMapByIndex(int32 Index)
{
	if (bUpdatingMapCombo || !AvailableMaps.IsValidIndex(Index))
	{
		return;
	}

	SelectedMapIndex = Index;
	WorkingConfig.SimulationMap = AvailableMaps[Index].Map;

	if (!AvailableMaps[Index].DisplayName.IsEmpty())
	{
		WorkingConfig.DisplayName = AvailableMaps[Index].DisplayName;
	}

	SynchronizeMapSelection();
	UpdateMapPreview();
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

#undef LOCTEXT_NAMESPACE
