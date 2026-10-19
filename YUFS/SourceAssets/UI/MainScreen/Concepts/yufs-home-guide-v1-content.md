# MainMenu 기본 사용 안내 구성

MainMenu의 네 카드를 프로그램 사용 안내로 구성하는 디자인 제안입니다. 기존 UMG 자산은 수정하지 않습니다.

| 기존 카드 | 새 내용 |
|---|---|
| Image_PreviewCard | 처음 사용하는 방법: 실험 조건 설정 → 실행·관찰 → 결과 확인 |
| Image_SettingsCard | 시작 전 확인: 맵, 행동 정책, 통신 시점, 기록 범위 |
| Image_RecentCard | 시간 설정은 이렇게 읽어요: 실험 시작과 화재 시작 기준의 차이 |
| Image_AnalysisCard | 결과에서 확인할 항목: 대피율, 평균 대피 시간, 대피 인원 |

실험 조건의 실제 입력은 ScenarioSetup에서만 수행하고, 홈 화면은 정적인 안내와 새 시뮬레이션으로 이동하는 버튼으로 구성합니다. 버튼은 기존 OnNewSimulationClicked / ShowScenarioSetup 흐름에 연결합니다.

시간 예시는 Fire Delay=30초, Alarm Offset=5초이면 실험 시작 35초에 경보가 발생한다는 뜻입니다. 현재 설정으로 오해하지 않도록 예시임을 표시합니다.

결과 항목은 YUFSResultsWidget의 대피율, 평균 대피 시간, 대피 인원 집계 함수와 대응합니다. 시안에 그려진 차트는 설명용 도형입니다. 실제 시간별 인원 그래프를 연결하지 않은 경우 대피 인원 설명은 '실험 종료 시 대피한 인원을 확인합니다'로 사용합니다.

시안은 내장 imagegen 도구로 제작했으며 최종 프롬프트는 yufs-home-guide-v1.prompt.txt에 저장했습니다. 구현 시 문구는 TextBlock으로 배치합니다.
