# WBP_ScenarioSetup 디자인 및 UMG 적용

## 결과물과 적용 범위

Concepts/yufs-scenario-setup-v1.png는 기존 메인화면의 밝은 회색 배경, 청록색 선택 상태, 흰색 카드와 얇은 경계선을 이어받은 화면 시안입니다. 내장 imagegen 도구로 제작했으며 최종 프롬프트를 같은 폴더에 저장했습니다. 첨부된 기존 WBP_ScenarioSetup의 구성요소를 모두 포함합니다.

이 패키지는 UI 디자인 이미지, 개별 제어 요소 PNG, UMG 배치·연결 명세입니다. 기존 WBP_ScenarioSetup.uasset 또는 블루프린트 Graph는 수정하지 않았습니다. 저장하지 않은 에디터 변경사항도 건드리지 않았습니다.

화면의 맵 그림은 디자인 예시입니다. 실제 구현에서는 맵 미선택 시 안내만 표시하고, 선택된 AvailableMaps 항목의 Thumbnail이 있을 때 해당 이미지를 표시합니다. 임의 건물 그림을 실제 선택 맵의 이미지로 사용하지 않습니다.

## 기존 구성요소 대응

| 기존 위젯 이름 | 타입 | 새 표시 이름 | 배치 카드 |
|---|---|---|---|
| MapCombo | ComboBoxString | 실험 맵 | 기본 설정 |
| MlPolicyCheck | CheckBox | ML 정책 사용 | 기본 설정 |
| FireDelaySpin | SpinBox | 화재 시작 지연 / Fire Delay | 기본 설정 |
| AlarmCheck | CheckBox | 화재 경보 사용 | 비상 통신 |
| AlarmOffsetSpin | SpinBox | Alarm Offset | 비상 통신 |
| PreMsgCheck | CheckBox | 사전 녹음 안내 사용 | 비상 통신 |
| PreMsgOffsetSpin | SpinBox | PreMsg Offset | 비상 통신 |
| LiveCheck | CheckBox | 실시간 방송 사용 | 비상 통신 |
| LiveOffsetSpin | SpinBox | Live Offset | 비상 통신 |
| StaffCheck | CheckBox | 직원 대피 유도 사용 | 비상 통신 |
| StaffOffsetSpin | SpinBox | Staff Offset | 비상 통신 |
| TimelineCheck | CheckBox | 기록 활성화 | 타임라인 기록 |
| TimelineEndSpin | SpinBox | 기록 종료 시점 / Timeline Offset | 타임라인 기록 |
| Btn_Start | Button | 시뮬레이션 시작 | 하단 동작 |
| Btn_Back | Button | 뒤로 | 하단 동작 |

TimelineCheck, TimelineEndSpin, Btn_Start, Btn_Back의 이름은 저장된 uasset의 문자열에서도 확인했습니다. 기존 위젯을 새 패널로 이동해 사용하면 위젯 참조와 기존 이벤트를 유지하기 편합니다. 새로 생성하는 경우 이름을 정확하게 맞추고 Graph 연결을 확인합니다.

## 기준 좌표

아래는 시안의 구성을 1920×1080에서 구현하기 위한 권장 좌표입니다. 생성 시안의 픽셀을 그대로 추적한 수치는 아니며, 하단 버튼과 상태바가 겹치지 않도록 정리한 배치 명세입니다. 모든 좌표는 루트 Canvas_Design 기준입니다. 전체화면으로 표시되는 기존 MenuSwitcher의 WBP_ScenarioSetup 페이지 안에 구성합니다. 홈 화면의 Canvas_MainHome 안에 넣지 않습니다.

| 영역 이름 | X | Y | W | H |
|---|---:|---:|---:|---:|
| Image_Background | 0 | 0 | 1920 | 1080 |
| Header | 0 | 0 | 1920 | 72 |
| Sidebar | 0 | 72 | 280 | 968 |
| Text_Title | 326 | 106 | 900 | 56 |
| Text_Description | 326 | 164 | 1200 | 36 |
| Card_Basic | 310 | 218 | 890 | 220 |
| Card_Communication | 310 | 456 | 890 | 350 |
| Card_Timeline | 310 | 824 | 890 | 126 |
| Card_MapPreview | 1218 | 218 | 678 | 370 |
| Card_Summary | 1218 | 606 | 678 | 344 |
| Btn_Back | 1394 | 972 | 200 | 60 |
| Btn_Start | 1610 | 972 | 286 | 60 |
| Text_ValidationError | 310 | 976 | 1050 | 52 |
| StatusBar | 0 | 1040 | 1920 | 40 |

Canvas의 직접 자식은 좌측 상단 앵커, Alignment=0/0, Auto Size 끄기로 설정합니다. 위치와 크기에는 논리 좌표를 입력하며 Designer의 DPI Scale을 곱하지 않습니다.

전체 화면을 1920×1080 기준으로 구성하려면 전체 Stretch의 ScaleBox(Scale To Fit) 안에 SizeBox(Width/Height Override=1920/1080)를 넣고 그 안에 Canvas_Design을 배치합니다. 다른 화면 비율에서는 여백이 생깁니다. 이미 자율 크기 조절이 구성되어 있으면 기존 DPI와 앵커 정책을 재사용하고 수동 Render Scale을 추가로 적용하지 않습니다.

## UMG 层级

```text
WBP_ScenarioSetup (parent: YUFSScenarioSetupWidget)
└─ Canvas_Root
   ├─ Image_OuterBackground
   └─ ScaleBox_Screen
      └─ SizeBox_Design (1920×1080)
         └─ Canvas_Design
            ├─ Image_Background
            ├─ Overlay_Header
            ├─ Overlay_Sidebar
            ├─ Text_Title
            ├─ Text_Description
            ├─ Overlay_Basic
            │  ├─ Image_Panel
            │  └─ Canvas_Basic
            │     ├─ Text_Heading
            │     ├─ MapCombo
            │     ├─ MlPolicyCheck (+ Text)
            │     └─ FireDelaySpin (+ label / unit)
            ├─ Overlay_Communication
            │  ├─ Image_Panel
            │  └─ VerticalBox_Communication
            │     ├─ Text_Heading
            │     ├─ Text_TimeReference
            │     ├─ HorizontalBox_Alarm (AlarmCheck / label / AlarmOffsetSpin / unit)
            │     ├─ HorizontalBox_PreMsg (PreMsgCheck / label / PreMsgOffsetSpin / unit)
            │     ├─ HorizontalBox_Live (LiveCheck / label / LiveOffsetSpin / unit)
            │     └─ HorizontalBox_Staff (StaffCheck / label / StaffOffsetSpin / unit)
            ├─ Overlay_Timeline
            │  ├─ Image_Panel
            │  └─ Canvas_Timeline
            │     ├─ Text_Heading
            │     ├─ TimelineCheck (+ Text)
            │     └─ TimelineEndSpin (+ label / unit)
            ├─ Overlay_MapPreview
            ├─ Overlay_Summary
            ├─ Text_ValidationError
            ├─ Btn_Back
            ├─ Btn_Start
            └─ Overlay_StatusBar
```

기존 ScrollBox는 루트 대신 필요한 카드 내부에서 사용하거나, 고정 크기 디자인이 모두 들어가면 제거합니다. 위젯을 이동하기 전 백업합니다. Overlay의 배경 Image와 콘텐츠는 모두 Fill로 설정합니다. 장식 이미지는 Not Hit-Testable, 버튼을 포함하는 패널은 Visible 또는 Not Hit-Testable (Self Only)로 두어 자식 입력을 허용합니다.

## 카드 내부 배치

- 기본 설정: 안쪽 여백 28~30. 제목 (28,20). Map 라벨 (28,76), MapCombo (150,66), 크기 710×52. ML 체크 영역 (28,148), 화재 지연 라벨 (466,148), FireDelaySpin (680,140), 크기 150×48. 단위 초는 오른쪽에 별도 TextBlock.
- 비상 통신: 안쪽 여백 28. 제목 (28,18), 설명 (28,56). 표 헤더 Y=94, 높이 34. 네 행의 Y는 132/184/236/288, 높이 48. 체크박스 X=30, 크기 28×28. 이벤트 설명 X=92, SpinBox X=640, 크기 160×42, 단위 X=814. 행마다 1px 구분선. 긴 한글/영문은 두 줄로 표시해 입력칸을 침범하지 않도록 구성.
- 타임라인: 제목 (28,15). TimelineCheck 영역 (28,65). 종료 시점 라벨 (368,28), 설명 (368,74), TimelineEndSpin (680,36), 크기 150×48. 문구는 '화재 시작 기준 · Timeline Offset'.
- 맵 미리보기: 제목 (28,20). 콘텐츠 영역 (28,66), 크기 622×238. Image는 ScaleBox(Scale To Fit) 안에 두어 썸네일 종횡비를 보존. 안내 문구 (28,322). 미선택이면 Image를 숨기고 안내를 표시.
- 설정 요약: 제목 (28,20), 요약 행 Y=68/108/148/188/228. 라벨 X=48, 값 X=280. 맵, 행동 정책, 화재 시작, 경보, 타임라인 종료를 표시. 설명 영역 (28,282), 크기 622×42.

## 이미지와 스타일

Shared는 기존 MainScreen 이미지 중 24개를 복사한 독립 패키지입니다. 이미 임포트한 이미지가 있으면 중복 임포트 없이 기존 텍스처를 재사용합니다. Controls는 새로 만든 18개 PNG입니다.

- 패널: Shared/panel_card.png, Draw As=Box, Margin=0.25(4방향), Brush Image Size=64×64.
- 숫자/드롭다운 배경: Controls/field_normal, field_hover, field_focus, field_disabled. Draw As=Box, Margin=0.25, Brush Image Size=64×64.
- 체크박스: Controls/check_unchecked_*와 check_checked_*; 원본 64×64, UI에서 28~32×28~32로 표시. Draw As=Image. CheckBox Style의 Unchecked Image / Hovered / Pressed 및 Checked Image / Hovered / Pressed에 각각 대응. 기본 CheckBox Style에는 개별 Disabled 이미지 슬롯이 없으므로 disabled 리소스를 사용하려면 상태별 브러시 교체를 구현하고, 간단히 구현할 때는 Is Enabled=false에 따른 기본 비활성 표현을 사용합니다.
- 시작 버튼: Shared/btn_primary_* 4개를 Normal/Hovered/Pressed/Disabled에 연결. 뒤로 버튼은 btn_secondary_* 사용. Draw As=Box / Margin=0.25.
- ComboBoxString: Combo Button Style 안의 Button Style에 입력 필드 브러시 연결. Down Arrow에는 chevron_down_slate 사용. 드롭다운 행 폰트도 본문 크기에 맞추고 선택·hover 색상은 연한 청록색으로 설정.
- SpinBox: 기존 SpinBox를 유지하고 Background/Hovered Background 계열 브러시, Foreground Color, Content Padding을 설정. 기본 SpinBox는 숫자 입력/드래그를 지원합니다. 시안의 위/아래 화살표까지 작동시키려면 별도 작은 Button 2개를 붙여 +/- 1초 후 SetValue와 설정 갱신을 호출합니다. 화살표를 장식으로만 넣어 조작 가능한 것처럼 보이게 하지 않습니다. 단순 구현에서는 화살표를 생략합니다.
- 텍스트는 PNG에 굽지 않고 TextBlock을 사용합니다. 주요 색 #253448, 보조 #526174, 비활성 #8A98A7, 주요 청록 #147F8A.
- 폰트 시작값: UMG Font Size 제목 28~32, 카드 제목 18~20, 입력/라벨 15~17, 도움말 12~14. 한글 지원 폰트 사용. 모든 텍스처 Tint는 흰색에서 시작.

## 시간의 의미

FireDelaySpin은 실험/레벨 시작부터 화재가 시작되기까지의 대기 시간입니다. 나머지 비상 통신 오프셋과 TimelineEndSpin은 화재 시작을 기준으로 계산합니다.

예: Fire Delay=30, Alarm Offset=5이면 실험 시작 30초에 화재, 35초에 경보입니다. TimelineEnd=60이면 화재 시작 60초 후, 즉 실험 시작 90초 시점에 타임라인 기록이 끝납니다. 이 값은 실험 전체의 종료 시간이 아니며, MaxSimDurationSeconds와 별개입니다.

## 기존 코드 연결

기준 코드: Source/YUFS/UI/YUFSScenarioSetupWidget.h/.cpp, Source/YUFS/Simulation/YUFSScenarioConfig.h/.cpp.

| 위젯 이벤트 | 연결 |
|---|---|
| MapCombo 선택 변경 | AvailableMaps와 같은 순서로 선택 인덱스 계산 → SelectMapByIndex(Index) → 썸네일/요약 갱신 |
| MlPolicyCheck 변경 | 체크: SetPolicyType(ML), 해제: SetPolicyType(RuleBased) |
| FireDelaySpin 값 변경 | SetFireStartDelay(Value) |
| AlarmCheck 변경 | SetAlarmEnabled(IsChecked, 표시 중인 유효 Offset) |
| PreMsgCheck 변경 | SetPreRecordedMsgEnabled(IsChecked, 표시 중인 유효 Offset) |
| LiveCheck 변경 | SetLiveAnnouncementEnabled(IsChecked, 표시 중인 유효 Offset) |
| StaffCheck 변경 | SetStaffGuidanceEnabled(IsChecked, 표시 중인 유효 Offset) |
| AlarmOffsetSpin 값 변경 | 체크 중일 때 WorkingConfig.AlarmTriggerOffsetSeconds 변경 |
| PreMsgOffsetSpin 값 변경 | 체크 중일 때 WorkingConfig.PreRecordedMsgOffsetSeconds 변경 |
| LiveOffsetSpin 값 변경 | 체크 중일 때 WorkingConfig.LiveAnnouncementOffsetSeconds 변경 |
| StaffOffsetSpin 값 변경 | 체크 중일 때 WorkingConfig.StaffGuidanceOffsetSeconds 변경 |
| TimelineCheck 변경 | WorkingConfig.bEnableTimelineRecording 변경 |
| TimelineEndSpin 값 변경 | WorkingConfig.TimelineRecordEndFireSeconds 변경 |
| Btn_Start 클릭 | ConfirmAndLaunch() |
| Btn_Back 클릭 | OnCancelled() → 부모 WBP_MainMenu에서 메인 페이지로 전환 |

WorkingConfig 멤버를 바꿀 때는 Blueprint의 Set Members in YUFSScenarioConfig로 해당 필드만 갱신합니다. 공용 함수가 없는 개별 Offset과 Timeline은 이 방법을 사용합니다. UI 값 갱신 도중에는 bRefreshingUI 같은 bool로 OnValueChanged/OnSelectionChanged 재진입을 막습니다.

체크 해제된 통신 Offset의 실제 설정값은 -1입니다. SpinBox에는 -1 대신 다음 활성화에 사용할 5/10/15/20초 등의 캐시값을 보여 주고 비활성화합니다. 체크를 켜면 해당 캐시값을 DefaultOffset 인자로 전달합니다. SpinBox를 비활성화하면서 값을 0으로 덮어써 실제 이벤트를 다시 켜지 않도록 주의합니다. 모든 시간 입력의 최소값은 0으로 설정합니다.

Timeline은 음수 비활성 규칙을 사용하지 않습니다. bEnableTimelineRecording이 false일 때 종료 SpinBox를 비활성화하고 기존 종료값은 유지합니다.

초기화는 기존 InitializeWith(Config)와 OnConfigRefreshed()를 이용합니다. MapCombo 옵션은 AvailableMaps의 DisplayName으로 채웁니다. 첫 맵이 자동 선택되는 기존 NativeConstruct 동작을 유지하면 맵 미선택 시안과 달리 실행 시 첫 맵이 표시될 수 있습니다. 이때 선택 맵의 썸네일과 요약을 표시하고 실제 검증 결과에 따라 시작 버튼을 활성화합니다. 시안을 맞추려고 유효한 선택값을 임의로 지우지 않습니다.

시작 버튼의 IsEnabled는 ValidateConfig의 결과로 갱신합니다. 맵 미선택 외에도 ML 선택 시 OnnxModelPath가 비어 있으면 현재 코드의 검증이 실패합니다. 기존 설정에서 모델 경로를 제공하고, 실패 이유를 Text_ValidationError 또는 OnValidationFailed(Reason)에 표시합니다. 이 디자인은 모델 경로 입력란 등 첨부 화면에 없던 입력을 추가하지 않았습니다.

새 시뮬레이션 메뉴: 기존 ShowScenarioSetup 이벤트에서 준비된 WBP_ScenarioSetup을 초기화한 뒤 MenuSwitcher의 해당 페이지로 전환합니다. 뒤로는 OnCancelled를 구현하여 기존 메인 페이지로 복귀시킵니다. MenuSwitcher의 페이지 순서와 기존 인덱스는 변경하지 않습니다. 실험 결과/환경 설정을 사이드바에 표시한다면 부모에 전환 요청을 전달하는 실제 동작도 연결합니다. 아직 연결하지 않은 메뉴는 숨기거나 비활성화합니다.

## 확인 항목

1. 스크린샷의 15개 기존 위젯이 모두 남아 있고 이름과 이벤트 연결이 유지되는지 확인.
2. 맵 선택, Policy 변경, 시간 입력, 각 체크박스가 WorkingConfig와 일치하는지 확인.
3. 통신 체크 해제 시 Offset=-1, 다시 체크 시 캐시값이 복원되는지 확인.
4. Timeline 체크는 기록 여부만 바꾸며 종료값을 비상 통신 이벤트로 처리하지 않는지 확인.
5. 맵/ML 검증 실패가 표시되고, 유효할 때만 시작 가능한지 확인.
6. 뒤로 이동 후 재진입 시 UI와 설정이 일치하는지 확인.
7. 1920×1080, 1280×720에서 카드와 입력란이 잘리지 않고 버튼을 클릭할 수 있는지 확인.

## 재생성

build-scenario-assets.ps1은 기존 MainScreen 세트에서 Shared를 복사하고, 신규 Controls와 요소 미리보기를 만듭니다. 생성 시안 이미지와 uasset은 변경하지 않습니다. build-scenario-assets.ps1을 실행하면 개별 PNG가 갱신되므로 수정본은 따로 보관합니다.
