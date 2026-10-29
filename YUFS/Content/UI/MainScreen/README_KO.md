# YUFS 메인화면 UI 이미지 세트

기존 메인화면 시안의 밝은 회색, 청록색, 얇은 선 아이콘을 기준으로 만든 UMG 조립용 이미지입니다. 버튼 문구와 실험 수치는 이미지에 포함하지 않았습니다. 언리얼 TextBlock으로 표시해 해상도와 언어 변경에 대응합니다. 이 폴더는 원본 이미지 세트이며, 프로젝트에 UMG 위젯이나 .uasset을 자동 생성하지는 않습니다.

## 저장된 이미지

| 폴더 | 내용 | 개수 |
|---|---|---:|
| Buttons | 주요·보조·메뉴 버튼의 Normal/Hovered/Pressed/Disabled, 선택된 메뉴, 선택 표시선 | 14 |
| Icons | 홈, 문서, 최근 기록, 결과, 설정, 종료, 폴더, 인원, 발화, 모델, 시간, 연기, 경로, 출구, 화살표, 최소화, 최대화, 닫기. 각각 slate/teal/white | 54 |
| Icons | 준비·경고·중립 상태 점 | 3 |
| Panels | 카드·소형 카드·범례 패널, 배경 4종, 구분선, 선택적 기술 격자 타일 | 9 |
| Brand | 투명 배경 YUFS 워드마크 | 1 |
| Illustrations | 투명 배경 건물 미리보기 | 1 |
| Preview | 요소 전체 보기와 조합된 메인화면 예시, 임포트 제외 | 2 |

총 82개 원본 PNG와 2개 확인용 PNG입니다. asset-manifest.json에 파일명, 크기, 용도, Brush 설정을 기록했습니다. 그림은 실제 시나리오 데이터가 아닌 메인화면용 예시입니다.

## UMG 적용

1. Buttons, Icons, Panels, Brand, Illustrations의 PNG를 Content/UI/MainScreen 등의 경로에 임포트합니다. Preview 폴더는 제외합니다.
2. UI 텍스처는 Compression Settings를 UserInterface2D (RGBA), Texture Group을 UI로 지정합니다. 일반 색상 이미지의 sRGB는 켭니다. UI 이미지가 흐려지면 Mip Gen Settings의 NoMipmaps를 사용합니다. 실제 엔진 버전에 따라 표시 이름이 다를 수 있습니다.
3. btn_*.png와 panel_*.png는 Brush Draw As를 Box로 지정하고 Margin을 Left/Top/Right/Bottom 모두 0.25로 설정합니다. 원본이 128×128이므로 32픽셀 테두리가 보존됩니다. Brush Image Size를 64×64 정도로 지정하면 약 8px 모서리 반경이 됩니다. 카드와 버튼의 최종 크기는 Slot/SizeBox로 정합니다.
4. 나머지는 Draw As = Image, Brush Tint = 흰색입니다. 아이콘은 원본 128×128을 24~32 논리 픽셀로 표시합니다. 선택 표시선은 폭 4~5px, 구분선은 높이 1px로 사용합니다. 아이콘의 투명 바깥 여백은 의도된 정렬용 여백입니다.
5. UMG Button Style의 Normal/Hovered/Pressed/Disabled에 동일 계열의 네 파일을 연결합니다. primary는 주요 실행, secondary는 불러오기, nav는 좌측 메뉴용입니다. 선택된 메뉴는 btn_nav_selected와 nav_selection_bar를 따로 표시합니다. 버튼 내부에 HorizontalBox + Image + TextBlock을 넣습니다.
6. 건물 이미지는 ScaleBox(Scale To Fit) 안의 Image로 표시하여 1536:1024 비율을 유지합니다. 반투명 그림자를 포함한 진짜 알파 채널이 있어 밝은 카드 위에 배치할 수 있습니다. 배경색은 panel_card 또는 workspace 색상을 사용합니다.
7. technical_grid_tile은 선택적으로 미리보기 아래에 사용할 수 있습니다. 이 이미지에는 틀이 없는 투명 격자만 들어 있습니다. 반복 사용 시 Brush Tiling 또는 반복 UV가 있는 UI 머티리얼을 사용합니다.

## 화면 구성 예시

기준 해상도는 1920×1080입니다. DPI Scaling과 앵커를 사용해 실제 화면에 맞춥니다.

| 영역 | 예시 위치/크기(px) | 구성 |
|---|---|---|
| 상단 | x0, y0 / 1920×72 | bg_header + logo/TextBlock + 준비 상태 |
| 좌측 | x0, y72 / 280×968 | bg_sidebar + 메뉴 버튼 + 하단 종료 |
| 제목 | x320, y106 | 실험 워크스페이스 + 설명 |
| 미리보기 | x310, y218 / 1150×580 | panel_card + 건물 ScaleBox + 범례 |
| 실험 설정 | x1478, y218 / 418×580 | panel_card + 설정 행 4개 + 버튼 2개 |
| 최근 실험 | x310, y819 / 750×202 | panel_card + 표 헤더 + 기록 행 |
| 분석 항목 | x1078, y819 / 818×202 | panel_card + subcard 3개 |
| 상태바 | x0, y1040 / 1920×40 | bg_header + 상태 점 + 준비됨 |

가로 배치는 HorizontalBox와 Fill/Auto Slot을 이용하는 것이 좋습니다. 미리보기 영역은 Fill, 설정 영역은 고정 또는 최소 폭을 설정합니다. 표와 수치는 실제 데이터를 TextBlock에 바인딩합니다. Preview/main-screen-assembled.png는 저장된 이미지들을 사용해 만든 조합 예시이며, 버튼 라벨과 60명 등의 값은 예시입니다.

## 메뉴 연결

Source/YUFS/UI/YUFSMainMenuWidget.h의 기존 함수를 이용합니다.

| 버튼 | 기존 연결 함수 |
|---|---|
| 새 시뮬레이션 / 새 실험 구성 | OnNewSimulationClicked |
| 최근 실험 불러오기 | OnContinueLastClicked |
| 실험 결과 | OnResultsClicked |
| 환경 설정 | OnSettingsClicked |
| 프로그램 종료 | OnQuitClicked |

실험 개요는 메인 패널 표시로 연결합니다. 최근 시나리오의 목록 화면은 별도 구성이 필요합니다. OnContinueLastClicked는 직전 시나리오를 다시 실행하므로, 확인·편집 후 실행하는 흐름을 원하면 별도의 불러오기 동작을 구현해야 합니다. 최근 기록이 없으면 불러오기 버튼을 비활성화합니다.

## 색상과 글자

- 주요 글자: #253448 / 보조 글자·아이콘: #526174
- 주요 버튼: #147F8A / Hover: #1995A1 / Pressed: #106570
- 선택 메뉴: #D9EFF2 / 배경: #F1F5F8 / 카드: #FFFFFF / 경계선: #DCE4EB
- 권장 크기: 메인 제목 36~40px, 카드 제목 24~26px, 메뉴·버튼 20~23px, 보조 글자 16~18px
- primary 글자는 흰색, disabled 글자는 #8A98A7, secondary 글자는 청록색으로 설정합니다.
- 한글을 포함한 프로젝트용 폰트를 UMG에 지정합니다. 로고 역시 TextBlock으로 표시할 수 있습니다.

## 제작 및 재생성

건물 그림은 내장 imagegen 도구로 기존 시안을 참조해 생성했습니다. 최종 생성 프롬프트는 Illustrations/building-preview.prompt.txt에 저장했습니다. 버튼·아이콘·패널·로고는 크기와 상태를 일정하게 유지하기 위해 System.Drawing으로 직접 디자인했습니다. PowerShell에서 다음 명령으로 해당 제어 요소와 조합 예시를 다시 만들 수 있습니다. 건물 그림은 재생성하지 않습니다.

```powershell
& .\build-ui-assets.ps1
& .\build-layout-preview.ps1
```

두 스크립트는 이 폴더의 제작 파일을 갱신하므로 수정한 이미지가 있다면 별도 복사본을 보관합니다.
