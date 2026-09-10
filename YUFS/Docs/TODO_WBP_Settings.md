# TODO: WBP_Settings 블루프린트 배선

메인 메뉴 "설정" 화면(`WBP_Settings`)의 C++ 백엔드(`UYUFSSettingsWidget`)는 완료됨.
남은 작업은 전부 언리얼 에디터에서 블루프린트로 UI를 만들고 배선하는 것.

## 진행 전 확인할 것
- [ ] 에디터에서 **Ctrl+Alt+F11**(Live Coding) 눌러서 최신 C++ 변경사항 컴파일했는지 확인.
  (컴파일 전에는 `YUFSSettingsWidget`이 "Pick Parent Class" 목록에 안 뜸)
- [ ] 컴파일 후 풀 빌드로 한 번 더 검증 필요 여부 확인 (에디터 켜져 있으면 Live Coding 락 때문에 커맨드라인 빌드 안 됨)

## 관련 C++ 파일
- `Source/YUFS/UI/YUFSSettingsWidget.h/.cpp` — 설정 화면 베이스 클래스
- `Source/YUFS/Simulation/YUFSGameInstance.h` — `bNPCDebugOverlayEnabled`
- `Source/YUFS/Debug/YUFSNPCDebugComponent.cpp` — `ShouldDraw()`에서 전역 토글 확인
- `Source/YUFS/UI/YUFSMainMenuWidget.h` — `ShowSettings()` (BlueprintImplementableEvent, 아직 빈 상태)

---

## 사전 준비
1. 새 위젯 블루프린트 생성 → 부모 클래스 **`YUFSSettingsWidget`** → 이름 `WBP_Settings`.
2. `WBP_MainMenu`의 WidgetSwitcher에 `WBP_Settings`를 네 번째 패널로 추가(Main/ScenarioSetup/Results와 동일한 위치).
   `UYUFSMainMenuWidget::ShowSettings()` 이벤트 그래프에서
   `WidgetSwitcher → Set Active Widget(WBP_Settings)`로 전환.

## A. 해상도 드롭다운 (ComboBoxString)
1. Designer에서 **ComboBox (String)** 배치 → 이름 `ResolutionComboBox`.
2. Graph 탭에서 **`Event Construct`** 노드 배치
   (C++ `NativeConstruct()`가 `RefreshAvailableResolutions()`를 이미 채운 뒤에 호출됨).
3. `Event Construct` → **For Loop**: `First Index = 0`, `Last Index = (Get Available Resolutions 의 Length - 1)`.
4. 루프 바디: `ResolutionComboBox → Add Option(Get Resolution Text(Loop Index))`.
5. For Loop **Completed** → `ResolutionComboBox → Set Selected Index(Selected Resolution Index)`.
6. `ResolutionComboBox` → Details → Events → **OnSelectionChanged** "+" 클릭.
   - 드롭다운 방식 대신 ◀▶ 버튼 2개로 `Select Resolution By Index(Selected Resolution Index ± 1)`을
     호출하는 게 더 간단함(문자열→인덱스 변환 불필요).
   - 드롭다운을 쓰려면 `OnSelectionChanged`가 주는 문자열로 `AvailableResolutions`를 순회해
     일치하는 Index를 찾은 뒤 `Select Resolution By Index` 호출.

## B. 창 모드 (버튼 3개 방식 — 가장 간단)
1. "전체 화면" / "테두리 없는 창" / "창 모드" 버튼 3개.
2. 각 버튼 `OnClicked → Set Window Mode(해당 enum 리터럴 하드코딩)`.
3. 현재 모드 표시(선택): 버튼 옆 체크 아이콘/텍스트가 현재 선택된 모드일 때만 보이게 하려면
   - 그 위젯 선택 → Details → **Visibility** 속성 옆 **Bind** 드롭다운 → **Create Binding** 클릭.
     → `Get <위젯이름>Visibility` 함수 그래프 자동 생성됨 (반환 타입 `ESlateVisibility`).
   - 함수 안에서 `Get Window Mode` → **Equal (Enum)**으로 해당 모드(예: `Fullscreen`)와 비교
     → 결과 bool을 **Select** 노드(또는 Branch)로 `Visible` / `Collapsed`에 매핑 → **Return Node** 연결.
   - 나머지 버튼들도 각자의 모드 Enum 리터럴로 동일 패턴 반복.
   - 순전히 시각적 표시용이라 급하지 않으면 생략 가능 (기능엔 영향 없음).

## C. VSync 체크박스
1. CheckBox → **IsChecked**를 `Bind` → `Is VSync Enabled`.
2. **OnCheckStateChanged** → `Set VSync Enabled(bIsChecked)`.

## D. 그래픽 품질 (버튼 4개, B와 동일 패턴)
"낮음/보통/높음/최고" 버튼 → 각 `OnClicked → Set Graphics Quality(해당 enum 값)`.

현재 값 표시(선택): B-3번과 동일 패턴.
- 해당 위젯 선택 → Details → **Visibility** 속성 옆 **Bind** 드롭다운 → **Create Binding** 클릭.
  → `Get <위젯이름>Visibility` 함수 그래프 자동 생성됨 (반환 타입 `ESlateVisibility`).
- 함수 안에서 `Get Graphics Quality` → **Equal (Enum)**으로 해당 값(예: `Low`)과 비교
  → 결과 bool을 **Select** 노드(또는 Branch)로 `Visible` / `Collapsed`에 매핑 → **Return Node** 연결.
- 나머지 버튼들도 각자의 품질 Enum 리터럴(`Medium`/`High`/`Epic`)로 동일 패턴 반복.
- 순전히 시각적 표시용이라 급하지 않으면 생략 가능 (기능엔 영향 없음).

## E. 적용 / 취소 버튼
- "적용" `OnClicked → Apply And Save Graphics Settings`.
- "취소" `OnClicked → Revert Graphics Settings` →
  이어서 `ResolutionComboBox → Set Selected Index(Selected Resolution Index)`도 한 번 더 호출
  (되돌린 뒤 ComboBox 선택 인덱스 갱신용).

## F. NPC 디버그 오버레이 토글
CheckBox → **IsChecked** `Bind` → `Is NPC Debug Overlay Enabled`.
**OnCheckStateChanged** → `Set NPC Debug Overlay Enabled(bIsChecked)`.

## G. 뒤로가기
`WBP_Results`/`WBP_ScenarioSetup`과 동일한 패턴:
"뒤로" 버튼 `OnClicked → Event OnClosed` 그래프에서
`Self → Get Parent → Cast To WidgetSwitcher → Set Active Widget Index(0)`.
(`Get Parent`는 반드시 `Self` 노드에서 드래그해서 만들어야 Target이 자동 연결됨.)

---

## 참고: 아직 안 만든 것들 (이전 세션에서 이미 완료된 것)
- `WBP_Results` + `WBP_ResultRow` (ListView 기반 회차별 결과, "이 회차 재현" 버튼) — 완료.
- 결과 저장 버그 수정(`EnterTimelineReviewMode`에서도 결과 저장) — 완료.
- 회차별 시드(`RandomSeed`) + 시나리오 스냅샷(`ScenarioConfig`) 저장, `ReplayRun()` — 완료.
