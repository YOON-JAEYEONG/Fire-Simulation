# 설정 화면 시안

기존 WBP_Settings의 해상도 콤보박스, 화면 모드 3개 버튼, 그래픽 품질 4개 버튼, NPC 디버깅 오버레이 체크박스, 적용·취소·뒤로 버튼을 유지한 디자인입니다. Main/ScenarioSetup/Results와 같은 회색 배경, 흰색 카드, 청록색 강조를 사용했습니다.

- 화면 설정 카드: 해상도와 전체 화면/테두리 없는 창/창 모드.
- 그래픽 품질 카드: 낮음/보통/높음/최고.
- 디버깅 카드: NPC 디버깅 오버레이.
- 우측 설정 안내: 화면 표시·품질·적용과 취소의 의미.
- 하단: 뒤로 / 취소 / 적용.

시안의 1920×1080, 테두리 없는 창, 높음 선택은 예시입니다. 실제 UI는 GetWindowMode, GetGraphicsQuality, SelectedResolutionIndex, IsNPCDebugOverlayEnabled의 값에 맞춥니다.

현재 코드에서 화면/그래픽 변경은 ApplyAndSaveGraphicsSettings로 적용·저장하고, RevertGraphicsSettings는 저장된 그래픽 설정을 다시 불러옵니다. NPC 디버깅 오버레이는 GameInstance의 값을 즉시 바꾸므로 그래픽 취소와 구분해서 설명했습니다. 뒤로는 OnClosed로 기존 메인 페이지에 복귀합니다.

내장 imagegen 도구로 제작했으며 최종 프롬프트를 yufs-settings-v1.prompt.txt에 저장했습니다. 디자인 시안만 저장했으며 기존 uasset과 설정값은 수정하지 않았습니다.
