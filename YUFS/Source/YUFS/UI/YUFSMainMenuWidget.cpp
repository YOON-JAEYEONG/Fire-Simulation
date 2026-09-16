#include "UI/YUFSMainMenuWidget.h"

#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"

void UYUFSMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 메뉴에서는 항상 마우스 커서 + UI 입력 모드
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->bShowMouseCursor = true;
		PC->SetInputMode(FInputModeUIOnly());
	}
}

UYUFSGameInstance* UYUFSMainMenuWidget::GetYUFSGameInstance() const
{
	return GetGameInstance() ? Cast<UYUFSGameInstance>(GetGameInstance()) : nullptr;
}

void UYUFSMainMenuWidget::OnNewSimulationClicked()
{
	ShowScenarioSetup();
}

void UYUFSMainMenuWidget::OnContinueLastClicked()
{
	if (UYUFSGameInstance* GI = GetYUFSGameInstance())
	{
		if (!GI->RelaunchLastScenario())
		{
			UE_LOG(LogTemp, Warning, TEXT("[YUFS] 이어서 실행할 시나리오가 없습니다."));
		}
	}
}

void UYUFSMainMenuWidget::OnResultsClicked()
{
	ShowResults();
}

void UYUFSMainMenuWidget::OnSettingsClicked()
{
	ShowSettings();
}

void UYUFSMainMenuWidget::OnQuitClicked()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

void UYUFSMainMenuWidget::LaunchScenario(const FYUFSScenarioConfig& Config)
{
	if (UYUFSGameInstance* GI = GetYUFSGameInstance())
	{
		GI->LaunchScenario(Config);
	}
}

bool UYUFSMainMenuWidget::HasLastScenario() const
{
	const UYUFSGameInstance* GI = GetYUFSGameInstance();
	return GI && GI->bHasActiveScenario;
}

FYUFSScenarioConfig UYUFSMainMenuWidget::GetLastScenario() const
{
	if (const UYUFSGameInstance* GI = GetYUFSGameInstance())
	{
		return GI->ActiveScenario;
	}
	return DefaultScenario;
}

bool UYUFSMainMenuWidget::HasLastResults() const
{
	const UYUFSGameInstance* GI = GetYUFSGameInstance();
	return GI && GI->LastRunResults.Num() > 0;
}

TArray<FSimRunResult> UYUFSMainMenuWidget::GetLastResults() const
{
	if (const UYUFSGameInstance* GI = GetYUFSGameInstance())
	{
		return GI->LastRunResults;
	}
	return {};
}
