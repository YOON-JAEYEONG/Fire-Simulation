#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "YUFSSettingsWidget.generated.h"

class UYUFSGameInstance;

// EWindowMode::Type과 1:1로 매핑됩니다. (그 enum은 UENUM/BlueprintType이 아니라서
// UI/설정 구조체에서 직접 쓸 수 없으므로 별도로 둡니다. EYUFSScenarioPolicy와 같은 이유.)
UENUM(BlueprintType)
enum class EYUFSWindowMode : uint8
{
	Fullscreen         UMETA(DisplayName = "전체 화면"),
	WindowedFullscreen UMETA(DisplayName = "테두리 없는 창"),
	Windowed           UMETA(DisplayName = "창 모드")
};

// UGameUserSettings::SetOverallScalabilityLevel(int32)의 0~3 값과 1:1로 매핑됩니다.
UENUM(BlueprintType)
enum class EYUFSGraphicsQuality : uint8
{
	Low    UMETA(DisplayName = "낮음"),
	Medium UMETA(DisplayName = "보통"),
	High   UMETA(DisplayName = "높음"),
	Epic   UMETA(DisplayName = "최고")
};

/**
 * YUFS 설정 화면 위젯 베이스 클래스.
 *
 * 메인 메뉴 "설정" 버튼(UYUFSMainMenuWidget::ShowSettings)에서 이 클래스를 부모로 하는
 * UMG 위젯 블루프린트(WBP_Settings)를 만들어 표시하세요.
 *
 * 그래픽 옵션은 엔진 내장 UGameUserSettings를 그대로 감싼 것입니다. Set* 함수들은 값을
 * 메모리에만 반영하고, [적용] 버튼에서 ApplyAndSaveGraphicsSettings()를 호출해야 실제
 * 화면에 적용되고 디스크에 저장됩니다(엔진 표준 관례).
 *
 * [뒤로] 버튼 → OnClosed() (BP에서 메인 메뉴 패널로 복귀 구현)
 */
UCLASS()
class YUFS_API UYUFSSettingsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

	// ── 해상도 ───────────────────────────────────────────────────────
	UPROPERTY(BlueprintReadOnly, Category = "YUFS|Settings")
	TArray<FIntPoint> AvailableResolutions;

	UPROPERTY(BlueprintReadOnly, Category = "YUFS|Settings")
	int32 SelectedResolutionIndex = INDEX_NONE;

	// 모니터가 지원하는 해상도 목록으로 AvailableResolutions를 채우고, 현재 해상도에 맞춰
	// SelectedResolutionIndex를 갱신합니다.
	UFUNCTION(BlueprintCallable, Category = "YUFS|Settings")
	void RefreshAvailableResolutions();

	UFUNCTION(BlueprintCallable, Category = "YUFS|Settings")
	void SelectResolutionByIndex(int32 Index);

	UFUNCTION(BlueprintPure, Category = "YUFS|Settings")
	FText GetResolutionText(int32 Index) const;

	// ── 창 모드 ──────────────────────────────────────────────────────
	UFUNCTION(BlueprintCallable, Category = "YUFS|Settings")
	void SetWindowMode(EYUFSWindowMode Mode);

	UFUNCTION(BlueprintPure, Category = "YUFS|Settings")
	EYUFSWindowMode GetWindowMode() const;

	// ── VSync ────────────────────────────────────────────────────────
	UFUNCTION(BlueprintCallable, Category = "YUFS|Settings")
	void SetVSyncEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "YUFS|Settings")
	bool IsVSyncEnabled() const;

	// ── 그래픽 품질 ─────────────────────────────────────────────────────
	UFUNCTION(BlueprintCallable, Category = "YUFS|Settings")
	void SetGraphicsQuality(EYUFSGraphicsQuality Quality);

	UFUNCTION(BlueprintPure, Category = "YUFS|Settings")
	EYUFSGraphicsQuality GetGraphicsQuality() const;

	// 지금까지 바꾼 값들을 실제로 화면에 적용하고 디스크에 저장합니다.
	UFUNCTION(BlueprintCallable, Category = "YUFS|Settings")
	void ApplyAndSaveGraphicsSettings();

	// 마지막으로 저장된 값으로 되돌립니다(변경 취소).
	UFUNCTION(BlueprintCallable, Category = "YUFS|Settings")
	void RevertGraphicsSettings();

	// ── NPC 디버그 오버레이 (전역 토글) ─────────────────────────────────
	UFUNCTION(BlueprintCallable, Category = "YUFS|Settings")
	void SetNPCDebugOverlayEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "YUFS|Settings")
	bool IsNPCDebugOverlayEnabled() const;

	// ── BP에서 구현하는 훅 ────────────────────────────────────────────
	// [뒤로] 버튼에서 호출 → 메인 메뉴 패널로 복귀
	UFUNCTION(BlueprintImplementableEvent, Category = "YUFS|Settings")
	void OnClosed();

protected:
	UYUFSGameInstance* GetYUFSGameInstance() const;
};
