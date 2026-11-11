# WBP_Settings 이미지 적용 순서

## 저장 파일

- StaticParts: 제목·아이콘·도움말과 우측 설정 안내 카드. UMG Image에 바로 넣는 원본 이미지 조각입니다.
- Skins: 글자가 없는 카드/버튼/입력칸/체크박스 배경. 기존 프로젝트 이미지의 복사본입니다.
- Cards_Reference: 설정 카드 전체를 잘라 둔 참고 이미지. 해상도와 선택 상태가 고정되어 있으므로 실제 입력 영역에는 사용하지 않습니다.
- Buttons_Reference: 라벨과 선택 표시가 포함된 버튼 조각. 참고 또는 라벨 포함 버튼 이미지가 필요할 때 사용합니다. 기본 적용은 Skins + 기존 버튼 Text입니다.
- Preview: 조각 확인용 이미지, 임포트 제외.

원본 1672×941에서 크기 변경 없이 27개 PNG를 잘랐고, 24개 재사용 스킨을 포함했습니다. 배경색을 그대로 포함한 조각이며 배경 제거한 투명 컷아웃은 아닙니다. 기존 WBP_Settings.uasset과 Graph는 수정하지 않았습니다.

## 1. 이미지 임포트

Content/UI/Settings 폴더를 만들고 StaticParts와 Skins의 PNG를 임포트합니다. 기존 MainScreen/ScenarioSetup에 같은 스킨을 이미 임포트했다면 그대로 재사용합니다. Texture Group=UI, Compression=UserInterface2D(RGBA), Mip Gen Settings=NoMipmaps, sRGB 켜기로 설정합니다.

StaticParts는 Draw As=Image, Tint=흰색으로 표시하고 종횡비를 유지합니다. 카드·버튼·입력칸 스킨은 Draw As=Box, Margin=네 방향 0.25, Brush Image Size=64×64로 표시합니다. 체크박스/화살표는 Image입니다. 제목 조각에는 글자가 이미 있으므로 같은 제목 TextBlock을 겹쳐 넣지 않습니다.

## 2. 기존 위젯을 보관한 채 루트 구성

PIE 실행을 중지하고 저장합니다. WBP_Settings의 Designer에서 루트 VerticalBox를 우클릭해 Wrap With → Canvas Panel로 감싸고 Canvas_Root로 이름을 바꿉니다. 기존 VerticalBox는 위젯을 옮길 때까지 보관합니다.

```text
Canvas_Root
├─ 기존 VerticalBox (임시)
├─ Image_OuterBackground
└─ ScaleBox_Screen
   └─ SizeBox_Design
      └─ Canvas_Design
```

Image_OuterBackground는 bg_workspace로 전체 Stretch, Offset 0. ScaleBox_Screen도 전체 Stretch/Offset 0, ZOrder=1, Stretch=Scale To Fit. SizeBox_Design은 Width Override=1920, Height Override=1080. Canvas_Design의 직접 자식들은 좌측 상단 앵커, Alignment 0/0, Auto Size 끄기입니다. DPI 수치를 위치에 직접 곱하지 않습니다.

## 3. 큰 영역 배치

아래 좌표는 모두 Canvas_Design 기준입니다. 생성 이미지의 픽셀을 그대로 확대한 것이 아니라 기존 페이지들과 같은 1920×1080 작업 영역에 맞춰 하단 버튼 공간을 확보한 권장 배치입니다.

| 위젯 | X | Y | W | H |
|---|---:|---:|---:|---:|
| Image_Background | 0 | 0 | 1920 | 1080 |
| Image_Header | 0 | 0 | 1920 | 72 |
| Image_Sidebar | 0 | 72 | 280 | 968 |
| Text_Title | 326 | 106 | 1000 | 56 |
| Text_Description | 326 | 164 | 1400 | 36 |
| Overlay_Display | 310 | 218 | 1032 | 300 |
| Overlay_Quality | 310 | 536 | 1032 | 210 |
| Overlay_Debug | 310 | 764 | 1032 | 196 |
| ScaleBox_Guide | 1360 | 218 | 536 | 742 |
| BackButton | 1360 | 972 | 170 | 60 |
| CancelButton | 1538 | 972 | 170 | 60 |
| ApplyButton | 1716 | 972 | 180 | 60 |
| Image_StatusBar | 0 | 1040 | 1920 | 40 |

상단 로고 YUFS는 (32,6), 프로그램명 '화재 · 대피 시뮬레이션'은 (206,24). 제목은 '환경 설정', 설명은 '화면 표시와 그래픽 품질, 디버깅 옵션을 조정합니다'. 좌측 메뉴는 기존 페이지와 같은 위치에 배치하고 설정 메뉴를 선택 상태로 표시합니다. 메뉴 동작을 복사할 때는 WBP_Settings의 부모에 해당 함수가 있는지 확인하고, 부모 MainMenu에 화면 전환 요청을 전달하는 기존 흐름을 이용합니다.

## 4. 세 카드 공통 구조

각 Overlay 안에 Image_Panel과 Canvas_Content를 넣습니다. 두 자식의 Overlay Slot은 가로/세로 Fill, Padding 0. Image_Panel은 Skins/panel_card입니다. 아래 콘텐츠 좌표는 각 카드 내부 기준입니다.

```text
Overlay_Display
├─ Image_Panel
└─ Canvas_Display
```

장식 Image는 Not Hit-Testable (Self & All Children). 실제 입력 위젯은 Visible. 입력 위젯을 포함하는 부모는 Visible 또는 Not Hit-Testable (Self Only)로 설정합니다. 부모 전체를 Self & All Children으로 지정하면 자식 입력도 막힙니다.

## 5. 화면 설정 카드

기존 ResolutionComboBox, FullscreenButton, WindowedFullscreenButton, WindowedButton을 기존 HorizontalBox/VerticalBox에서 Canvas_Display로 이동합니다. 해당 위젯 자체의 이름과 이벤트는 유지합니다.

| 요소 | 카드 내부 X | Y | W | H |
|---|---:|---:|---:|---:|
| Image_HeadingDisplay (heading_display.png) | 24 | 18 | 260 | 70 |
| Text_ResolutionLabel: 해상도 | 136 | 108 | 140 | 32 |
| ResolutionComboBox | 290 | 90 | 500 | 60 |
| Text_ModeLabel: 화면 모드 | 136 | 188 | 140 | 32 |
| FullscreenButton | 290 | 170 | 230 | 60 |
| WindowedFullscreenButton | 532 | 170 | 230 | 60 |
| WindowedButton | 774 | 170 | 230 | 60 |
| Image_HintDisplay (hint_display.png) | 136 | 260 | 756 | 28 |

ResolutionComboBox의 Font Size는 16 전후, Content Padding은 가로 14/세로 8 정도로 시작합니다. Combo Button Style > Button Style의 Normal=field_normal, Hovered=field_hover, Pressed=field_focus, Down Arrow=chevron_down_slate. 배경 Brush는 Box/Margin 0.25입니다. 실제 옵션과 선택값은 기존 기능을 유지합니다. 시안의 1920×1080을 고정 이미지로 덮지 않습니다.

화면 모드 버튼 라벨은 기존 '전체 화면', '테두리 없는 창', '창 모드'를 유지합니다. Font Size=16 정도. Normal=btn_secondary_normal, Hovered=btn_secondary_hover, Pressed=btn_secondary_pressed, Disabled=btn_secondary_disabled. 선택 버튼의 Normal은 btn_secondary_pressed 또는 field_focus로 표시하고 글자는 #147F8A로 바꿉니다. 비선택 글자는 #253448입니다. 선택된 버튼도 다른 모드로 전환할 수 있도록 불필요하게 비활성화하지 않습니다.

## 6. 그래픽 품질 카드

기존 LowQualityButton, MediumQualityButton, HighQualityButton, EpicQualityButton을 Canvas_Quality로 이동합니다.

| 요소 | 내부 X | Y | W | H |
|---|---:|---:|---:|---:|
| Image_HeadingQuality (heading_quality.png) | 24 | 18 | 260 | 70 |
| LowQualityButton | 136 | 87 | 207 | 56 |
| MediumQualityButton | 355 | 87 | 207 | 56 |
| HighQualityButton | 574 | 87 | 207 | 56 |
| EpicQualityButton | 793 | 87 | 207 | 56 |
| Image_HintQuality (hint_quality.png) | 136 | 174 | 756 | 27 |

버튼 라벨은 낮음/보통/높음/최고. 화면 모드와 같은 secondary 계열 스킨을 적용합니다. 기존 버튼 Text를 유지하고 PNG 라벨을 추가로 겹치지 않습니다.

## 7. 디버깅 카드

기존 NPCDebugOverlayCheckBox를 Canvas_Debug로 이동합니다. 체크박스 안에 기존 Text가 들어 있다면 Text를 밖으로 옮겨 별도 라벨로 배치한 뒤 체크박스 크기를 28×28로 지정합니다.

| 요소 | 내부 X | Y | W | H |
|---|---:|---:|---:|---:|
| Image_HeadingDebug (heading_debug.png) | 24 | 18 | 220 | 70 |
| NPCDebugOverlayCheckBox | 136 | 86 | 28 | 28 |
| Text_DebugLabel: NPC 디버깅 오버레이 | 180 | 82 | 700 | 32 |
| Text_DebugDescription | 180 | 118 | 750 | 24 |
| Image_DebugHint (hint_debug_immediate.png) | 136 | 148 | 756 | 43 |

설명: 'NPC 디버깅 정보를 화면에 표시합니다'. 체크박스 Style의 Unchecked/Unchecked Hovered/Unchecked Pressed에 check_unchecked_normal/hover/pressed를 각각 연결합니다. Checked/Checked Hovered/Checked Pressed에는 check_checked_normal/hover/pressed를 연결합니다. Brush Draw As=Image, Image Size=28×28, Tint=흰색입니다.

## 8. 오른쪽 안내 카드

ScaleBox_Guide의 Stretch=Scale To Fit. 자식 Image_Guide에 StaticParts/card_settings_guide.png를 지정합니다. Brush Image Size=465×681, Draw As=Image, Tint=흰색. Overlay용 추가 panel_card를 뒤에 넣지 않습니다. 이미 카드 테두리와 문구가 포함된 이미지입니다. 같은 설명 TextBlock도 추가하지 않습니다. StaticParts/guide_* 조각은 안내를 분리해 배치하려는 경우의 대안입니다.

## 9. 하단 세 버튼

기존 BackButton, CancelButton, ApplyButton을 Canvas_Design의 직접 자식으로 이동하고 3번 표의 좌표와 크기를 입력합니다. 앞의 두 버튼은 secondary 계열, 적용은 primary 계열 스킨을 사용합니다. Normal/Hovered/Pressed/Disabled를 각각 지정하며, 모든 브러시 Draw As=Box, Margin=0.25, Image Size=64×64. 라벨은 기존 Text로 표시합니다. 적용 글자는 흰색, 뒤로와 취소는 #147F8A. Normal Padding과 Pressed Padding을 동일하게 맞춥니다.

## 10. 기존 이벤트 연결 확인

기존 연결이 있으면 중복 이벤트를 만들지 않습니다. 다음 함수는 YUFSSettingsWidget에서 제공하는 함수이며, Target이 별도의 GameUserSettings 객체인 동명의 함수와 혼동하지 않습니다.

| 위젯 이벤트 | 함수 |
|---|---|
| ResolutionComboBox 선택 | SelectResolutionByIndex(Index) |
| FullscreenButton 클릭 | SetWindowMode(Fullscreen) |
| WindowedFullscreenButton 클릭 | SetWindowMode(WindowedFullscreen) |
| WindowedButton 클릭 | SetWindowMode(Windowed) |
| LowQualityButton 클릭 | SetGraphicsQuality(Low) |
| MediumQualityButton 클릭 | SetGraphicsQuality(Medium) |
| HighQualityButton 클릭 | SetGraphicsQuality(High) |
| EpicQualityButton 클릭 | SetGraphicsQuality(Epic) |
| NPCDebugOverlayCheckBox 변경 | SetNPCDebugOverlayEnabled(IsChecked) |
| ApplyButton 클릭 | ApplyAndSaveGraphicsSettings() |
| CancelButton 클릭 | RevertGraphicsSettings() → UI 값과 선택 표시 재동기화 |
| BackButton 클릭 | OnClosed() → 기존 메인 페이지로 복귀 |

## 11. 선택 표시와 초기화

RefreshSettingsUI라는 Blueprint 함수를 만들어 다음을 수행합니다. bRefreshingUI=true로 시작해 값 표시 중 선택 이벤트의 재진입을 막고 마지막에 false로 바꿉니다. 기존 Event Construct나 이벤트가 있으면 그 흐름에 통합합니다.

- 해상도 옵션을 채울 때 AvailableResolutions와 동일한 순서로 GetResolutionText(Index)를 Add Option합니다. 현재 목록이 이미 채워져 있으면 다시 중복 추가하지 않습니다. SelectedResolutionIndex가 유효하면 해당 항목을 선택합니다. Clear Options, Set Selected Index로 발생하는 선택 이벤트는 bRefreshingUI인 동안 설정에 재반영하지 않습니다.
- GetWindowMode와 각 버튼의 모드를 비교하여 선택 버튼만 선택 스타일로 갱신합니다.
- GetGraphicsQuality와 각 버튼의 품질을 비교하여 선택 버튼만 선택 스타일로 갱신합니다.
- NPCDebugOverlayCheckBox는 IsNPCDebugOverlayEnabled에 맞춥니다. 이미 Checked State 바인딩이 있으면 해당 바인딩을 재사용합니다.

선택 스타일은 Button의 Set Style로 설정할 수 있습니다. Make ButtonStyle 또는 기존 ButtonStyle의 Set Members를 사용해 Normal을 선택/비선택 브러시로 설정하고 Hovered/Pressed/Disabled도 지정합니다. 전환할 때 모든 관련 버튼의 비선택 스타일도 갱신해야 이전 선택 표시가 남지 않습니다. 매 Tick에서 값을 강제로 재설정하지 않습니다.

초기화 시점, 모드·품질 선택 후, 취소 후, 설정 페이지 재진입 시 RefreshSettingsUI를 호출합니다. 버튼 선택 직후에는 전체 콤보 옵션을 다시 만들 필요 없이 선택 스타일만 갱신해도 됩니다.

## 12. 적용·취소의 의미

현재 코드에서 그래픽 관련 Set 함수는 엔진 설정 객체의 값을 바꾸고 ApplyAndSaveGraphicsSettings가 적용·저장합니다. RevertGraphicsSettings는 저장된 화면·그래픽 값을 불러와 적용합니다. NPC 디버깅 오버레이는 GameInstance 값을 즉시 바꾸므로 그래픽 취소로 되돌아가지 않습니다.

BackButton의 OnClosed 자체에는 적용·취소 로직이 없습니다. 기존 복귀 흐름을 유지하고, 변경사항을 버리고 돌아가는 정책을 원하면 그래픽 Revert 후 복귀하도록 별도로 연결합니다. 이 패키지는 그 동작을 자동 변경하지 않습니다.

## 13. 정리와 확인

필요한 기존 위젯을 모두 옮긴 후 빈 VerticalBox/HorizontalBox만 삭제합니다. 이름과 Graph 참조를 확인하고 Compile/Save합니다. 실제 창/해상도 변경은 에디터 내 고정 PIE 뷰포트와 동작이 다를 수 있으므로 Standalone 또는 패키징 실행에서도 확인합니다.

모드·품질 선택 표시, 취소 후 표시 복원, 해상도 선택, NPC 체크 전환, 적용, 뒤로, 1280×720에서 입력 가능 여부를 확인합니다. 사진처럼 모든 카드를 한 장 이미지로 덮지 말고 실제 조작 위젯 위에는 장식 Image가 입력을 가리지 않도록 합니다.
