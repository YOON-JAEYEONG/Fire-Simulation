# 메인화면 안내 이미지 조각

원본 yufs-home-guide-v1.png를 지정 좌표로 자른 PNG입니다. 원본을 재생성하거나 글자를 다시 그리지 않았습니다. 배경과 글자가 포함된 이미지이며, 배경을 지운 투명 아이콘 세트는 아닙니다. 원본과 같은 픽셀 크기로 저장하여 추가 확대나 축소를 하지 않았습니다.

## 가장 간단한 적용: Cards의 네 이미지 사용

| 기존 MainMenu 이미지 | 교체할 PNG |
|---|---|
| Image_PreviewCard | Cards/home_how_to_use.png |
| Image_SettingsCard | Cards/home_before_start.png |
| Image_RecentCard | Cards/home_time_example.png |
| Image_AnalysisCard | Cards/home_results_guide.png |

이 네 이미지는 카드 테두리, 설명, 그림을 모두 포함합니다. 기존 카드 배경 Image의 Brush 이미지를 해당 PNG로 교체하거나, 해당 카드 영역 안에 Image를 하나 추가해 표시합니다. 기존 테두리가 이중으로 보이면 원래 카드 배경은 숨깁니다.

1. Cards 폴더의 PNG를 Unreal 콘텐츠 브라우저에 임포트합니다.
2. 텍스처 설정: Compression=UserInterface2D(RGBA), Texture Group=UI, Mip Gen Settings=NoMipmaps, sRGB 켜기.
3. UMG Image의 Brush > Image에 해당 텍스처를 지정합니다.
4. Draw As는 **Image**로 지정합니다. 글자까지 포함된 완성 카드이므로 Box/Border 또는 Margin 0.25로 늘리지 않습니다.
5. Tint와 Color and Opacity는 흰색/알파 1로 설정합니다.
6. 크기는 아래 비율을 유지합니다. 기존 슬롯의 비율이 다르면 ScaleBox(Scale To Fit) 안에 Image를 넣습니다. Image의 Brush Image Size는 원본 크기로 입력합니다.
7. 장식 Image의 Visibility를 Not Hit-Testable (Self & All Children)로 지정합니다.

| 파일 | 원본 크기 | 1920 가로 기준 권장 표시 크기(근사) |
|---|---|---|
| home_how_to_use.png | 1001×493 | 1149×566 |
| home_before_start.png | 364×493 | 418×566 |
| home_time_example.png | 652×191 | 749×219 |
| home_results_guide.png | 713×191 | 819×219 |

위 네 이미지의 권장 위치·크기는 slice-manifest.json의 reference1920에 기록했습니다. 원본이 1672×941이므로 1920/1672 비율을 적용한 근사값입니다. 현재 위젯의 기존 카드 높이와 약간 다를 수 있습니다. 텍스트를 찌그러뜨리지 않도록 기존 카드 슬롯에 Scale To Fit으로 배치하는 방법이 편합니다.

## '새 시뮬레이션 시작' 클릭 영역

Cards/home_how_to_use.png에는 버튼 모양이 이미 포함되어 있지만 Image 자체는 클릭 동작을 갖지 않습니다. 다음 두 방법 중 하나를 사용합니다.

### 방법 A: 완성 카드 위에 투명 Button 배치

카드와 버튼을 같은 Canvas 또는 Overlay의 콘텐츠 Canvas에 넣습니다. 버튼은 카드보다 앞쪽에 표시합니다. Button의 Normal/Hovered/Pressed/Disabled Brush는 Draw As=No Draw Type으로 설정하고 콘텐츠는 비워둡니다. 버튼 Visibility는 Visible, 카드 Image는 Not Hit-Testable로 설정합니다.

카드를 원본 1001×493으로 표시할 때 버튼의 **카드 내부 좌표**:

```text
X=721, Y=414, Width=256, Height=59
```

카드가 균일하게 확대되면 위 네 값에 같은 배율을 곱합니다. 예를 들어 가로를 1150으로 표시하면 배율은 1150/1001이고 버튼의 내부 위치는 약 X=828.3, Y=475.6, 크기는 294.1×67.8입니다. ScaleBox 바깥쪽에 버튼을 따로 놓으면 여백 계산이 달라질 수 있으므로 **카드와 버튼을 같은 논리 좌표의 Canvas에 넣고 그 Canvas 전체를 ScaleBox로 감싸는 방식**이 안전합니다.

```text
SizeBox 또는 고정 크기 슬롯
└─ ScaleBox (Scale To Fit)
   └─ SizeBox (1001×493)
      └─ Canvas
         ├─ Image (0,0 / 1001×493)
         └─ Button (721,414 / 256×59)
```

Button의 OnClicked를 기존 OnNewSimulationClicked에 연결하면 ShowScenarioSetup 흐름을 사용할 수 있습니다. 투명 버튼은 기본적으로 hover 색상 변화가 없으므로 필요하면 별도 hover 장식을 추가합니다.

### 방법 B: 분리된 요소로 조합

- Parts/how_to_content.png: 제목과 세 단계 안내만 포함.
- Parts/start_hint_and_button.png: 하단 안내와 버튼을 함께 포함.
- Parts/start_hint.png: 안내 문구만 포함.
- Buttons/btn_start_simulation.png: 버튼 이미지와 라벨 포함.

Buttons의 이미지를 UMG Button Style의 Normal에 넣고 별도 TextBlock을 추가하지 않습니다. Hovered와 Pressed에 같은 이미지를 쓰고 Tint를 약간 조정할 수 있습니다. 버튼 자체의 네이티브 비율은 256:59입니다. 이 방법에서는 완성 카드와 분리 요소를 중복해서 표시하지 않도록 합니다.

## Parts 폴더

각 단계 전체, 단계별 그림, 체크 항목, 시간 흐름도, 결과 항목 등을 개별 배치할 때 사용합니다. 모든 조각은 해당 위치의 원본 배경색을 그대로 포함합니다. 완성 카드 안에 이미 같은 내용이 있으므로, 전체 카드와 조각을 동시에 배치할 필요는 없습니다.

- step_01_setup / step_02_run / step_03_results: 단계 번호·글자·그림.
- illustration_setup / illustration_evacuation / illustration_results: 설명용 그림만.
- check_map / check_policy / check_communication / check_recording: 우측 안내 항목.
- time_diagram / time_example_caption: 시간 예시.
- result_evacuation_rate / result_average_time / result_people / results_hint: 결과 안내.

## 해상도와 문구

이 패키지는 요청한 대로 글자를 포함한 원본 이미지 조각입니다. 크게 확대하면 원본 해상도 한계가 보일 수 있습니다. 고해상도 가변 UI나 문구 수정이 필요하면 기존 개별 패널/아이콘 + UMG TextBlock 방식으로 전환합니다.

원본의 '대피 인원' 설명에는 시간에 따른 변화라는 문구가 들어 있습니다. 현재 실제 결과 화면이 최종 집계 인원만 제공한다면 이 설명을 정확히 맞추려면 원본 문구 수정이 필요합니다. 이 세트는 요청한 원본을 그대로 자르는 범위로 제작했습니다.

Preview/cards-contact-sheet.png는 잘린 카드 네 개를 확인하는 용도이며 임포트할 필요가 없습니다. slice-home-guide.ps1로 같은 원본에서 조각을 재생성할 수 있습니다. validation.json은 원본 픽셀 보존 검사 결과입니다.
