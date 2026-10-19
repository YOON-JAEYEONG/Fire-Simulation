# C++ 시나리오 맵 미리보기 적용

YUFSScenarioSetupWidget에서 맵 목록과 선택 상태, 썸네일을 자동으로 동기화합니다. 기존 uasset과 Graph는 수정하지 않았습니다.

1. C++ 변경을 빌드한 뒤 WBP_ScenarioSetup을 열어 Compile합니다. 부모 클래스는 YUFSScenarioSetupWidget을 유지합니다.
2. 기존 맵 드롭다운의 이름은 MapCombo, 타입은 ComboBoxString으로 둡니다.
3. 미리보기 Image 이름을 Image_MapThumbnail으로 지정합니다. ScaleBox 아래에 배치하고 ScaleBox의 Stretch를 Scale To Fit으로 둡니다. Image의 Color and Opacity는 흰색, Alpha=1로 설정합니다.
4. 안내 TextBlock이 있으면 이름을 Text_MapPreviewHint로 지정합니다. 없으면 안내 문구 부분만 생략됩니다.
5. Class Defaults → Available Maps에 항목을 추가하고 DisplayName, Map, Thumbnail을 지정합니다. Thumbnail은 임포트한 Texture2D입니다. 실제로 MenuSwitcher에 배치된 인스턴스가 이 배열을 덮어쓰고 있다면 그 값도 확인합니다.
6. 실행하면 처음 표시할 때와 맵을 변경할 때 썸네일이 갱신됩니다. 이미지가 없는 맵으로 변경하면 이전 이미지를 지우고 안내를 표시합니다.

MapCombo, Image_MapThumbnail, Text_MapPreviewHint는 BindWidgetOptional이므로 아직 없는 UI에서도 해당 부분만 생략합니다. 위젯 이름과 타입이 일치하면 C++에서 연결합니다. 블루프린트에서 같은 이름의 일반 변수를 별도로 만들지 않습니다.

맵 목록은 AvailableMaps 순서로 자동 생성합니다. 빈 이름은 '맵 N', 중복 이름은 괄호 번호로 구분합니다. Blueprint에서 별도로 Clear Options/Add Option으로 같은 목록을 채우거나, On Selection Changed에서 다시 맵을 선택할 필요가 없습니다. 이미 연결해 둔 경우 목록 생성·맵 선택·미리보기 갱신 부분은 중복 실행하지 않도록 정리하고, 다른 설정 요약 갱신 같은 로직은 유지합니다.

InitializeWith로 설정을 전달해도 선택값과 이미지가 함께 갱신됩니다. 실행 중 AvailableMaps를 변경한 경우 RefreshMapOptions를 호출합니다. 선택한 맵의 Thumbnail만 교체한 경우 UpdateMapPreview를 호출할 수 있습니다. 두 함수는 BlueprintCallable입니다.

이 변경은 미리보기 표시를 구현하며 썸네일 이미지를 자동 촬영하지 않습니다. 맵이 없는 경우에도 임의의 맵을 새로 만들거나 로드하지 않습니다. 초기 맵이 비어 있고 AvailableMaps에 항목이 있으면 기존과 같이 첫 맵을 기본 선택합니다.

자동화 테스트: YUFS.UI.ScenarioMapPreview. 선택 변경, 중복 이름, 누락된 썸네일의 이전 이미지 제거, InitializeWith 동기화, 선택 위젯 누락, 재구성 시 이벤트 재연결을 확인합니다.
