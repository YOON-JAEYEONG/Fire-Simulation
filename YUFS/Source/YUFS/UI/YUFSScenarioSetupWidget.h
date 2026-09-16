#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Simulation/YUFSScenarioConfig.h"
#include "YUFSScenarioSetupWidget.generated.h"

class UYUFSGameInstance;

/**
 * YUFS 시나리오 설정 위젯 베이스 클래스.
 *
 * "새 시뮬레이션"을 눌렀을 때 나오는 설정 화면입니다. 에디터에서 이 클래스를 부모로 하는
 * UMG 위젯 블루프린트(WBP_ScenarioSetup)를 만들고, 각 컨트롤을 WorkingConfig 필드에
 * 바인딩하거나 아래 Set* 헬퍼에 연결하세요.
 *
 * [시작] 버튼 → ConfirmAndLaunch()
 * [뒤로] 버튼 → OnCancelled() (BP에서 메인 메뉴로 복귀 구현)
 */
UCLASS()
class YUFS_API UYUFSScenarioSetupWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

	// 편집 중인 설정. UMG 컨트롤과 양방향으로 연결합니다.
	UPROPERTY(BlueprintReadWrite, Category = "YUFS|Setup")
	FYUFSScenarioConfig WorkingConfig;

	// 화면에서 고를 수 있는 맵 목록 (에디터에서 채웁니다).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "YUFS|Setup")
	TArray<FYUFSScenarioMapOption> AvailableMaps;

	// 현재 선택된 맵 인덱스 (AvailableMaps 기준, 미선택 시 INDEX_NONE)
	UPROPERTY(BlueprintReadOnly, Category = "YUFS|Setup")
	int32 SelectedMapIndex = INDEX_NONE;

	// 메인 메뉴 등에서 초기값을 넘겨 화면을 채웁니다.
	UFUNCTION(BlueprintCallable, Category = "YUFS|Setup")
	void InitializeWith(const FYUFSScenarioConfig& Config);

	// ── 개별 값 설정 헬퍼 (UMG OnValueChanged 등에 연결) ──────────────
	UFUNCTION(BlueprintCallable, Category = "YUFS|Setup")
	void SelectMapByIndex(int32 Index);

	UFUNCTION(BlueprintCallable, Category = "YUFS|Setup")
	void SetNPCCount(int32 Count);

	UFUNCTION(BlueprintCallable, Category = "YUFS|Setup")
	void SetPolicyType(EYUFSScenarioPolicy Policy);

	UFUNCTION(BlueprintCallable, Category = "YUFS|Setup")
	void SetPlacementPreset(EYUFSPlacementPreset Preset);

	UFUNCTION(BlueprintCallable, Category = "YUFS|Setup")
	void SetFireStartDelay(float Seconds);

	// 통신 이벤트 on/off — off면 오프셋을 -1(비활성)로, on이면 DefaultOffset으로 설정
	UFUNCTION(BlueprintCallable, Category = "YUFS|Setup")
	void SetAlarmEnabled(bool bEnabled, float DefaultOffset = 5.f);

	UFUNCTION(BlueprintCallable, Category = "YUFS|Setup")
	void SetPreRecordedMsgEnabled(bool bEnabled, float DefaultOffset = 10.f);

	UFUNCTION(BlueprintCallable, Category = "YUFS|Setup")
	void SetLiveAnnouncementEnabled(bool bEnabled, float DefaultOffset = 15.f);

	UFUNCTION(BlueprintCallable, Category = "YUFS|Setup")
	void SetStaffGuidanceEnabled(bool bEnabled, float DefaultOffset = 20.f);

	// ── 확정 / 취소 ─────────────────────────────────────────────────────
	UFUNCTION(BlueprintCallable, Category = "YUFS|Setup")
	bool ValidateConfig(FText& OutError) const;

	// WorkingConfig를 검증하고, 통과하면 GameInstance에 저장 후 맵을 로드합니다.
	UFUNCTION(BlueprintCallable, Category = "YUFS|Setup")
	void ConfirmAndLaunch();

	// ── BP에서 구현하는 훅 ──────────────────────────────────────────────
	// [뒤로] 버튼에서 호출 → 메인 메뉴로 복귀
	UFUNCTION(BlueprintImplementableEvent, Category = "YUFS|Setup")
	void OnCancelled();

	// 검증 실패 시 사유를 화면에 표시
	UFUNCTION(BlueprintImplementableEvent, Category = "YUFS|Setup")
	void OnValidationFailed(const FText& Reason);

	// InitializeWith 이후 UMG 컨트롤 값을 WorkingConfig에 맞추도록 갱신
	UFUNCTION(BlueprintImplementableEvent, Category = "YUFS|Setup")
	void OnConfigRefreshed();

protected:
	UYUFSGameInstance* GetYUFSGameInstance() const;
};
