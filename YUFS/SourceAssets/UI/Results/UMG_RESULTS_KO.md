# WBP_Results / WBP_ResultRow 화면 구성

## 디자인 범위

메인·ScenarioSetup과 같은 회색 바탕, 흰색 카드, 청록 버튼을 사용하는 결과 보기 시안입니다. 상단 요약 카드 4개, 실행 시각별 결과 표, 행별 다시 실행 버튼, 하단 뒤로 버튼으로 구성합니다. 원본 uasset과 실행 데이터는 수정하지 않았습니다. 시안의 다섯 기록과 요약값은 디자인용 예시입니다.

기존 결과 목록의 모든 정보를 유지합니다: 실행 시각, 대피 인원/전체 인원, 대피율, 사망 인원, 실험 소요시간, 평균 대피 시간, 다시 실행.

## 현재 화면에서 바꿀 부분

기존 WBP_Results의 VerticalBox에 요약 Text와 ListView, 뒤로 버튼이 직접 들어 있는 상태를 화면용 Canvas와 카드로 재배치합니다. ResultListView 자체와 기존 동작은 유지합니다. WBP_ResultRow의 HorizontalBox는 셀별 고정 너비와 행 높이를 지정합니다. TextBlock의 자동 크기에 열 간격을 맡기면 행마다 길이가 달라져 숫자와 버튼이 붙거나 어긋납니다.

스크린샷은 PIE 시뮬레이션 중입니다. Designer/Graph를 수정할 때는 먼저 실행을 중지합니다. 두 위젯의 부모는 각각 YUFSResultsWidget, YUFSResultRowWidget으로 유지합니다.

## WBP_Results 권장 좌표: 1920×1080 논리 좌표

| 요소 | X | Y | W | H |
|---|---:|---:|---:|---:|
| Header | 0 | 0 | 1920 | 72 |
| Sidebar | 0 | 72 | 280 | 968 |
| Text_Title | 326 | 106 | 1000 | 56 |
| Text_Description | 326 | 164 | 1400 | 36 |
| Card_RunCount | 310 | 218 | 383 | 128 |
| Card_AverageRate | 711 | 218 | 383 | 128 |
| Card_AverageDuration | 1112 | 218 | 383 | 128 |
| Card_AverageEvacuationTime | 1513 | 218 | 383 | 128 |
| Card_ResultsTable | 310 | 366 | 1586 | 574 |
| Text_ReplayHint | 310 | 960 | 1320 | 56 |
| Btn_Back | 1696 | 972 | 200 | 60 |
| StatusBar | 0 | 1040 | 1920 | 40 |

좌표는 구현용 권장값이며 생성된 시안의 픽셀과 일대일 대응하지 않습니다. Canvas 직속은 좌측 상단 앵커, Alignment 0/0. 기존 전체화면 디자인과 같은 ScaleBox/1920×1080 SizeBox 구조를 사용할 수 있습니다.

```text
WBP_Results
└─ Canvas_Root
   ├─ Image_OuterBackground
   └─ ScaleBox_Screen (Scale To Fit)
      └─ SizeBox_Design (1920×1080)
         └─ Canvas_Design
            ├─ Image_Background
            ├─ Header
            ├─ Sidebar
            ├─ Text_Title
            ├─ Text_Description
            ├─ Overlay_RunCount
            ├─ Overlay_AverageRate
            ├─ Overlay_AverageDuration
            ├─ Overlay_AverageEvacuationTime
            ├─ Overlay_ResultsTable
            │  ├─ Image_TableBackground
            │  └─ Canvas_TableContent
            │     ├─ Text_TableTitle
            │     ├─ HorizontalBox_ColumnHeaders
            │     ├─ ResultListView
            │     └─ Text_EmptyState
            ├─ Text_ReplayHint
            ├─ Btn_Back
            └─ StatusBar
```

기존 VerticalBox를 Canvas로 감싼 뒤 각 위젯을 새 부모로 이동하고, 빈 컨테이너를 마지막에 삭제하는 방식으로 구성할 수 있습니다.

## 공통 스타일

MainScreen/ScenarioSetup에 이미 임포트한 panel_card, bg_workspace, bg_header, bg_sidebar, btn_secondary_*를 재사용합니다. 카드와 버튼 배경은 Draw As=Box, Margin=0.25, Brush Image Size=64×64. 숫자와 문구는 이미지로 굽지 않고 TextBlock에 연결합니다.

- 배경 #F1F5F8, 카드 #FFFFFF, 경계선 #DCE4EB.
- 주요 글자 #253448, 보조 #526174, 청록 #147F8A.
- UMG Font Size 시작값: 화면 제목 30, 요약 숫자 26~30, 카드 제목 18~20, 표 본문 14~16, 도움말 12~14.
- 행 VerticalAlignment=Center, 텍스트 Wrap 끄기. 숫자는 정렬을 통일합니다.
- 0 사망 인원은 중립색으로 표시합니다. 임의의 대피율 합격/불합격 임계값은 만들지 않습니다.

## 상단 요약 카드 연결

| 라벨 | YUFSResultsWidget 함수 | 표시 |
|---|---|---|
| 실험 횟수 | GetRunCount | 정수 + 회 |
| 평균 대피율 | GetAverageEvacuationRate | Percent 형식, 소수 1자리 |
| 평균 소요시간 | GetAverageDurationSeconds | 소수 1자리 + 초 |
| 평균 대피 시간 | GetAverageEvacuationTime | 소수 1자리 + 초 |

GetAverageEvacuationRate는 0~1 값입니다. As Percent를 사용하면 그대로 입력하고, 수동 숫자 변환 시에만 100을 곱합니다. 둘을 동시에 적용하지 않습니다.

현재 GetAverageEvacuationTime은 대피자가 있는 회차들의 평균 대피 시간을 다시 평균한 값입니다. 전체 대피 인원에 대한 가중 평균이 아닙니다. GetAverageDurationSeconds는 실험 전체 소요시간의 평균입니다. 두 시간을 같은 값으로 연결하지 않습니다.

## 표와 행의 공통 열 너비

표 카드 내부 제목 (32,22). 헤더 위치 (32,86), 크기 1504×48. ResultListView 위치 (32,134), 크기 1522×400. ListView 폭에 스크롤바 여유 18px을 둡니다. 실제 스크롤바와 패딩에 맞추어 이 여유를 조정하고, 헤더와 행의 본문 너비는 동일하게 유지합니다.

| 열 | 너비 | WBP_ResultRow 데이터 |
|---|---:|---|
| 실행 시각 | 280 | GetRunIndexText |
| 대피 인원 | 200 | GetEvacuationSummaryText |
| 대피율 | 170 | GetEvacuationRateText |
| 사망 인원 | 160 | GetIncapacitatedText |
| 소요시간 | 170 | GetDurationText |
| 평균 대피 시간 | 300 | GetAverageEvacuationTimeText |
| 재실행 | 224 | Button → ReplayThisRun |
| 합계 | 1504 | 헤더와 본문 공통 |

현재 Getter에는 '18 / 20명 대피', '사망 0명', '평균 대피 시간 35.0초'처럼 라벨이 포함됩니다. 함수에 바로 바인딩하면 이 형태가 표시되는 것이 정상입니다. 시안처럼 짧은 숫자만 보여 주려면 GetRunResult → Break SimRunResult의 필드를 Format Text로 가공합니다. 기존 Get*Text를 사용해도 기능상 문제는 없습니다.

대피율 아래의 작은 막대는 선택 사항입니다. 셀 내부를 VerticalBox로 구성해 TextBlock과 높이 6px 정도의 ProgressBar를 배치합니다. ProgressBar.Percent에는 GetRunResult의 EvacuationRate(0~1)를 그대로 사용합니다. 100을 곱하지 않습니다. 막대 배경은 #DCE4EB, 채움은 #147F8A로 지정합니다.

실행 시각은 기존 함수 이름이 GetRunIndexText이지만 현재 구현은 회차 번호가 아닌 Timestamp를 표시합니다. 열 제목을 '회차 번호'로 바꾸지 않습니다.

## WBP_ResultRow 구성

```text
WBP_ResultRow (YUFSResultRowWidget)
└─ SizeBox_Row (Height Override=72)
   └─ Overlay_Row
      ├─ Image_RowBackground
      └─ HorizontalBox_Cells
         ├─ SizeBox (Width=280) → Text_Timestamp
         ├─ SizeBox (Width=200) → Text_EvacuationCount
         ├─ SizeBox (Width=170) → Text_EvacuationRate
         ├─ SizeBox (Width=160) → Text_Incapacitated
         ├─ SizeBox (Width=170) → Text_Duration
         ├─ SizeBox (Width=300) → Text_AverageEvacuationTime
         └─ SizeBox (Width=224) → Button_Replay
```

각 HorizontalBox Slot은 Auto로 두고, SizeBox가 열 폭을 결정하게 합니다. 각 셀 안쪽 Text의 Padding은 좌우 12 전후, 세로 정렬 Center. Button_Replay는 약 150×40, 셀 중앙 정렬로 배치합니다. 행 전체 높이를 1080으로 고정하지 않습니다. Designer 미리보기만 전체화면처럼 보이면 미리보기 크기를 가로 약 1504, 세로 72로 조정합니다. ListView의 실제 항목 높이는 루트 SizeBox의 Desired Size로 제어합니다.

행 배경은 흰색, 구분선은 하단 1px. 버튼 Normal/Hovered/Pressed는 기존 secondary 계열 사용. 행 hover/선택 강조를 추가할 때는 이전 선택 색상이 재사용된 행에 남지 않도록 상태 변경 시 원래 색상도 설정합니다. 필수 기능은 선택 없이도 동작하도록 구성합니다.

## ListView 갱신

ResultListView의 Entry Widget Class=WBP_ResultRow, Orientation=Vertical, Vertical Entry Spacing=4~6, Selection Mode는 Single 또는 None. ListView를 별도 ScrollBox 안에 중첩하지 않습니다. 표 카드 안의 지정된 영역을 채우게 설정합니다.

```text
WBP_Results: Event OnResultsRefreshed
    → Get Result Entries
    → ResultListView.Set List Items
    → 상단 네 요약 카드 갱신
    → HasResults에 따라 목록/빈 상태 표시
```

현재 C++의 NativeConstruct에서 RefreshResults를 호출하고, RefreshResults가 OnResultsRefreshed를 호출합니다. 동일한 이벤트 안에서 다시 RefreshResults를 호출하면 재귀가 되므로 넣지 않습니다. 메인 메뉴에서 결과 페이지를 다시 열 때 최신 결과가 필요하면 해당 진입 흐름에서 RefreshResults를 호출합니다.

ListView에는 WBP_ResultRow 위젯을 직접 생성해 AddChild 하지 않고, 기존 GetResultEntries가 돌려주는 UObject 항목을 Set List Items로 전달합니다.

행의 C++ NativeOnListItemObjectSet이 전달된 항목을 CachedResult에 저장하고 OnRowDataSet을 호출합니다. 기존 Get*Text 바인딩이 이미 있으면 유지할 수 있습니다. 이벤트 기반으로 구성한다면 WBP_ResultRow의 OnRowDataSet에서 TextBlock 여섯 개를 갱신하고 재실행 버튼의 IsEnabled도 갱신합니다. 초기 Construct에서만 텍스트를 채우면 재사용되는 행의 데이터가 갱신되지 않을 수 있습니다.

## 다시 실행과 뒤로

```text
WBP_ResultRow:
Button_Replay.OnClicked → ReplayThisRun()
OnRowDataSet → Button_Replay.SetIsEnabled(CanReplayThisRun())

WBP_Results:
Btn_Back.OnClicked → OnClosed()
OnClosed 구현 → 기존 메인 메뉴 페이지로 전환
```

ReplayThisRun은 해당 행에 저장된 시나리오 설정과 랜덤 시드로 실행을 요청하는 기능입니다. 시뮬레이션 영상을 재생하는 동영상 플레이어 기능이 아닙니다. 예전 기록에 유효한 시나리오 정보가 없으면 CanReplayThisRun=false로 버튼이 비활성화될 수 있습니다.

## 빈 결과와 데이터 표시

HasResults=false이면 표 대신 '표시할 실험 결과가 없습니다. 새 시뮬레이션을 실행해 주세요.'를 표시합니다. 평균값은 '-'로 표시하면 아직 결과가 없다는 의미를 명확히 전달할 수 있습니다. 예시 숫자를 실제 UI의 고정 텍스트로 남기지 않습니다.

스크린샷의 0001-01-01 시각은 초기화되지 않은 Timestamp일 가능성이 있습니다. 가로 간격을 늘려도 날짜 데이터는 바뀌지 않습니다. TotalNPCCount=0인 기록도 저장 데이터 확인이 필요하며, 디자인 변경 과정에서 임의 숫자로 덮어쓰지 않습니다. 사망 인원이라는 표시는 현재 GetIncapacitatedText의 문구를 유지한 것입니다.

## 검증

기록 0개, 1개, 스크롤이 필요한 다수 기록에서 확인합니다. 열 제목과 행의 정렬, 긴 날짜 문자열, 행 높이, 스크롤 후 데이터 갱신, 각 행의 정확한 재실행 대상, 뒤로 이동을 확인합니다. 1280×720에서도 글자가 겹치지 않는지 점검합니다.
