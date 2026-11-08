#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Simulation/YUFSScenarioConfig.h"
#include "Simulation/YUFSGameInstance.h"
#include "YUFSMainMenuWidget.generated.h"

/**
 * YUFS 메인 메뉴 위젯 베이스 클래스.
 *
 * 에디터에서 이 클래스를 부모로 하는 UMG 위젯 블루프린트(WBP_MainMenu)를 만들어 사용합니다.
 * 버튼 OnClicked 이벤트를 아래 On*Clicked 함수에 연결하면 됩니다.
 *
 * 화면 전환(시나리오 설정 패널 열기, 결과 화면 열기 등)은 위젯 스왑/서브 위젯 구성의
 * 자유도를 위해 BlueprintImplementableEvent로 위임합니다.
 */
UCLASS()
class YUFS_API UYUFSMainMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

	// ── 메뉴 버튼 바인딩 ───────────────────────────────────────────────
	// "새 시뮬레이션" → 시나리오 설정 화면으로 (ShowScenarioSetup 구현 필요)
	UFUNCTION(BlueprintCallable, Category = "YUFS|MainMenu")
	void OnNewSimulationClicked();

	// "이어서 실행" → 직전에 실행한 시나리오를 그대로 다시 실행
	UFUNCTION(BlueprintCallable, Category = "YUFS|MainMenu")
	void OnContinueLastClicked();

	// "결과 보기" → 결과 화면으로 (ShowResults 구현 필요)
	UFUNCTION(BlueprintCallable, Category = "YUFS|MainMenu")
	void OnResultsClicked();

	// "설정" → 설정 화면으로 (ShowSettings 구현 필요)
	UFUNCTION(BlueprintCallable, Category = "YUFS|MainMenu")
	void OnSettingsClicked();

	// "종료"
	UFUNCTION(BlueprintCallable, Category = "YUFS|MainMenu")
	void OnQuitClicked();

	// 시나리오 설정 화면이 확정한 설정으로 시뮬레이션 시작.
	// (설정 화면을 별도 위젯으로 두었다면 그 위젯에서 이 함수를 호출하거나
	//  UYUFSGameInstance::LaunchScenario를 직접 호출해도 됩니다.)
	UFUNCTION(BlueprintCallable, Category = "YUFS|MainMenu")
	void LaunchScenario(const FYUFSScenarioConfig& Config);

	// ── 상태 조회 (버튼 활성/표시 제어용) ─────────────────────────────
	UFUNCTION(BlueprintPure, Category = "YUFS|MainMenu")
	bool HasLastScenario() const;

	UFUNCTION(BlueprintPure, Category = "YUFS|MainMenu")
	FYUFSScenarioConfig GetLastScenario() const;

	UFUNCTION(BlueprintPure, Category = "YUFS|MainMenu")
	bool HasLastResults() const;

	UFUNCTION(BlueprintPure, Category = "YUFS|MainMenu")
	TArray<FSimRunResult> GetLastResults() const;

	// 설정 화면 초기값 / 빠른 시작에 사용할 기본 시나리오
	UFUNCTION(BlueprintPure, Category = "YUFS|MainMenu")
	FYUFSScenarioConfig GetDefaultScenario() const { return DefaultScenario; }

	// ── BP에서 구현하는 화면 전환 훅 ─────────────────────────────────
	UFUNCTION(BlueprintImplementableEvent, Category = "YUFS|MainMenu")
	void ShowScenarioSetup();

	UFUNCTION(BlueprintImplementableEvent, Category = "YUFS|MainMenu")
	void ShowResults();

	UFUNCTION(BlueprintImplementableEvent, Category = "YUFS|MainMenu")
	void ShowSettings();

protected:
	// 시나리오 설정 화면의 초기값. 에디터에서 기본 맵/인원/타이밍을 지정합니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "YUFS|MainMenu")
	FYUFSScenarioConfig DefaultScenario;

	UYUFSGameInstance* GetYUFSGameInstance() const;
};
