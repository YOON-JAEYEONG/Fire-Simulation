# JJW_NPC_BEHAVIOR 우선 병합 안내

## 목적과 기준

`zion-fs`의 상호작용 기능을 합치되, NPC의 판단·대피·경로 탐색은 `JJW_NPC_BEHAVIOR`를 우선한다. 소화기·문 기능이 별도의 인격이나 이동 시스템을 만들어 JJW 행동을 덮어쓰지 않도록 통합한다.

| 구분 | 기준 |
|---|---|
| 병합 전 로컬 작업 보존 체크포인트 | `ae8efed` |
| 복구용 브랜치 | `codex/backup-zion-fs-before-jjw-20260914` |
| 우선 적용 대상 | `JJW_NPC_BEHAVIOR`의 `204a8d3` |
| 최종 병합 커밋 | 이 문서를 포함하는 두 부모 병합 커밋. `git log -1 --merges`로 확인 |
| 원격 반영 | 아직 푸시하지 않음 |

이 문서의 **구현 설명과 검증 결과는 구분한다.** 자동 테스트 통과는 실제 건물의 모든 배치·문·화재 장면에 대한 시각 검증을 의미하지 않는다.

## 어떤 부분을 우선했는가

| 영역 | 병합 원칙 | 관련 코드 |
|---|---|---|
| 개인별 성격과 반응 | JJW의 성격 시드, 경보 신뢰도, 반응 지연, 준비 시간, 속도·경로 선호를 유지한다. 모든 사람을 같은 시간에 강제로 대피시키지 않는다. | [BehaviorStateMachine](../Source/YUFS/NPC/Behavior/YUFSBehaviorStateMachine.cpp) |
| 대피 결정 | 대피 결심과 준비 단계는 JJW 상태머신이 결정한다. 상호작용 측에서 위험도나 상태를 강제로 투영하지 않는다. | [NPC](../Source/YUFS/NPC/YUFSEvacuationNPC.cpp) |
| 화재 지각 | 직접 보거나 가까이 감지한 열·연기, 가림, 개인별 관측을 사용한다. 소화기 발견이나 FDS 메타데이터 존재만으로 화재를 인지했다고 가정하지 않는다. | [Perception](../Source/YUFS/NPC/Perception/YUFSNPCPerceptionComponent.cpp) |
| 개인별 위험 기억 | JJW의 최대 8192셀·기본 30초 기억과 관측 갱신을 사용한다. 경로 계산에 넘긴 스냅샷은 나중에 바뀐 기억으로 덮어쓰지 않는다. | [HazardField](../Source/YUFS/Fire/YUFSHazardField.h) |
| 경로 탐색 | JJW의 비동기 요청, 취소 세대, 최신 목적지, 완전한 경로 판정, 경로 비용 계산을 유지한다. 상호작용은 이 경로 체계를 이용한다. | [Navigator](../Source/YUFS/NPC/Navigation/YUFSSmokeAwareNavigator.cpp) |
| 좁은 통로와 막힘 | JJW의 양보·국소 이동·충돌 및 바닥 확인 기반 복구를 사용한다. 상호작용이 순간이동이나 충돌 해제로 길을 뚫지 않는다. | [LocalMovement](../Source/YUFS/NPC/Navigation/YUFSLocalMovementComponent.cpp) |
| 학습 모델 계약 | 기존 28개 입력과 11개 행동의 순서·폭을 유지한다. 추가 실행 제약은 런타임 정보로 다룬다. | [Observation](../Source/YUFS/Core/YUFSObservation.h) |
| 배치와 화면 | JJW의 NPC 팔레트, 드래그 배치, 회전·취소·삭제, 선택 표시를 우선한다. 배치한 NPC와 플레이어 카메라를 자동으로 옮기지 않는다. | [DropZone](../Source/YUFS/UI/YUFSDropZoneWidget.cpp) |

정규화된 열 값은 FDS 내보내기 데이터의 표현이며 **섭씨 온도가 아니다.** 실측 행동 비율이나 화재 안전 기준으로 해석하면 안 된다.

## 상호작용을 붙인 방식

```text
JJW 지각·개인 성격·상태머신
        │
        ├─ 아직 대피 결심 전 / 준비 중 → JJW 행동 유지
        │
        └─ 대피를 결심하고 실행 가능한 상태
                 │
                 ├─ 소화 조건 미충족 → 기존 대피 경로 유지
                 │
                 └─ 관측한 소화기 + 검증된 FDS 목표 + 개인 위험·퇴로 조건 충족
                          │
                          ├─ 확률에 따라 시도하지 않음 → 대피
                          └─ 시도 선택 → 소화기 접근·획득 → 접근 지점 → 소화 동작
                                                       │
                                                       └─ 위험 상승 / 조건 소실 → 중단·대피
```

### 소화기

- 대피 결심을 하지 않았거나 준비 중인 NPC를 소화 행동으로 강제 전환하지 않는다. 확률을 100%로 설정해도 필수 행동·위험·정보 조건을 우회할 수 없다.
- JJW가 허용한 상태에서만 선택적 상호작용을 평가한다. 개인 위험도, 즉각적 열·연기 위험, 행동불능·저자세 이동, 공식 대피 지시 등은 선택 확률보다 우선한다.
- 소화 중에도 위험 조건을 다시 확인한다. 위험이 높아졌는데 시연 시간을 채우려고 계속 소화하지 않는다.
- 발화점은 소화 방향을 정하는 목표다. 이동 목적지는 발화점 자체가 아니라 검증된 **소화 접근 지점**이어야 한다.
- NPC는 소화 동작을 수행할 뿐, 기록된 FDS 화재·연기 값을 줄이지 않는다. 임의의 진압 성공, 불 삭제, 새로운 불 효과를 만들지 않는다.
- 퇴로 정보나 위험 데이터가 없어 선택적 접근을 승인할 수 없다면 소화 시도를 열지 않는다. 자료 누락을 안전하다는 증거로 사용하지 않는다.
- 선택기가 제안한 행동과 실제 승인·게시한 행동을 구분한다. 실행기는 현재 팀 계약에 게시된 소화 지시가 철회되면 과거 제안만으로 계속 소화하지 않는다.
- 소화 확률은 모든 NPC 중 일정 인원을 강제로 배정하는 비율이 아니다. 조건을 충족한 개인이 해당 화재에 대해 한 번 선택하는 확률이며, 소화기를 바꿔 반복 추첨하지 않는다.

관련 코드: [소화 실행](../Source/YUFS/NPC/Integration/YUFSNpcSuppressionComponent.cpp), [위험 조건](../Source/YUFS/NPC/Integration/YUFSSuppressionSafety.h), [행동 선택](../Source/YUFS/NPC/Decision/YUFSHumanBehaviorSelectorComponent.cpp), [팀 간 실행 계약](../Source/YUFS/NPC/Integration/YUFSTeamIntegrationTypes.h).

### 문과 다른 NPC

- 문 사용은 기존 목적지·경로 흐름에 연결한다. 예약, 몸 방향 정렬, 손 뻗기, 개방, 통과 순서로 처리한다.
- 사람이나 벽이 문 회전을 막으면 충돌을 무시하고 열지 않는다. 문이 열렸다는 이유로 문 또는 NPC를 숨겨 통과를 연출하지 않는다.
- 상호작용 종료 후 동일한 NPC가 기존 판단·대피 체계로 돌아간다. 도움을 주고받던 사람을 별도 시연용 인물로 교체하지 않는다.
- 도움을 받은 사람에게는 관측한 동료 안내 신호를 전달한다. 대피 결심을 강제로 기록하거나 준비 시간을 건너뛰지 않는다. JJW가 직접 결정한 `Helping → HelpOther` 행동도 보존한다.
- 문 조작·대화 중 정지는 경로가 막힌 것으로 오인하지 않는다. 경로 계산 대기·실패 중 매 프레임 요청을 재시작하지 않으며, JJW의 재시도 판정을 따른다.
- 일시정지·종료·타임라인 관찰에서는 새로운 AI 행동이나 상호작용 진행이 기록 재생과 경쟁하지 않도록 한다.

관련 코드: [문](../Source/YUFS/Fire/YUFSInteractionDoor.cpp), [환경 상호작용](../Source/YUFS/NPC/Integration/YUFSNpcEnvironmentInteraction.cpp).

### 애니메이션 호환성

정식 `BP_YUFSRLEvacuationNPC`는 기존 상호작용 애니메이션과 같은 스켈레톤을 사용한다. 반면 예전 `BP_YUFSEvacuationNPC`는 Manny 스켈레톤을 사용하므로 동일 시퀀스를 강제로 재생하면 안 된다. 초기화 전에 11개 행동 및 저자세·행동불능의 실제 바인딩을 검사하고, 호환되지 않으면 **원래 AnimBP·애니메이션 모드·메시를 유지**한다. 외부 모션 담당을 사용 중인 NPC도 네이티브 초기화로 덮어쓰지 않는다.

이는 충돌 방지용 호환 처리이며 리타기팅이 아니다. Manny에서 동일한 소화 표현이 필요하면 모션 담당이 그 스켈레톤에 맞는 바인딩을 제공해야 한다. [애니메이션 컴포넌트](../Source/YUFS/NPC/Animation/YUFSActionAnimationComponent.cpp)와 [실제 에셋 기반 회귀 테스트](../Source/YUFS/Tests/JJWAnimationFallbackTests.cpp)에 계약을 남겼다.

## 제거하거나 비활성화한 중복 동작

- 상호작용 측에서 JJW 상태머신의 위험도·행동 상태를 강제로 덮어쓰는 기존 변경 API.
- 내비게이터에 따로 존재하던 256개 위험 패치 모델. 위험 기억은 JJW 지각의 셀 기억을 사용한다.
- 기본 자동 NPC 분산, 대표 NPC 애니메이션 전시, 자동 카메라 포커스.
- 병합 과정에서 선언·호출만 남았던 자동 분산·전시 helper. 대규모 시연 코드를 다시 붙이지 않고 미사용 경로를 제거했다.
- 일반 실행에서 환경 소품을 배치한 뒤 자동으로 시뮬레이션을 시작하던 동작.

기존 Blueprint 참조를 깨지 않기 위해 남긴 전시 API는 호환용이다. 전시 시작 호출로 NPC 위치나 카메라가 바뀌지 않는다. 옛 에셋에 자동 전시 설정이 남아 있어도 BeginPlay에서 이를 비활성화한다.

## 실행·UI·집계 변경

1. 일반 실행은 **대기 → 사용자가 NPC 배치·회전 → 시작 버튼** 순서다.
2. `-YUFSBuildingInteractions`는 상호작용 환경 초기화 옵션이지 자동 시작 옵션이 아니다. 자동 검증 실행에 필요한 경우에만 별도 `-YUFSAutoStartSimulation`을 지정한다.
3. 상호작용 훈련 인구를 덮어쓰는 `OverrideInteractionTrainingPopulation`은 기본 false다. 활성화해도 상호작용용 FireTraining만 다루며 JJW 성격·대피 반응을 덮어쓰지 않는다.
4. HUD 미지정 시 정식 JJW 경로 `/Game/Blueprint/UI/WBP_SimHUD.WBP_SimHUD_C`를 사용한다. 명시한 사용자 HUD 클래스는 보존한다.
5. 기존 `/Game/Blueprint/WBP_SimHUD` 리디렉터와 새로운 `Blueprint/UI` 에셋을 함께 유지한다. 선택 머티리얼, PostProcessVolume, `r.CustomDepth=3`도 JJW UI 구성의 일부다.
6. 타임라인을 위해 남겨 둔 대피 완료 NPC를 전체 인원에 다시 더하지 않는다. 완료를 중복 호출해도 결과가 중복 생성되지 않도록 했다.
7. 시작 전 취소·삭제와 중도 등록 해제를 집계에 반영한다. 해결되지 않은 NPC의 철회는 대피나 사망으로 만들지 않으며, 이미 해결된 NPC의 표시 액터 삭제는 과거 결과를 지우지 않는다.

관련 코드: [GameInstance](../Source/YUFS/Simulation/YUFSGameInstance.cpp), [SimulationController](../Source/YUFS/Simulation/YUFSSimulationController.cpp).

## 아직 필요한 외부 자료와 제한

### JJW Scenario_04 화재 에셋

JJW가 변경한 기존 화재 액터는 다음 머티리얼을 참조한다.

`/Game/Fires/vdb/Scenario_04/vdb/SparseVolumeMaterial_Inst_Scenario_04`

확인 시점에 이 파일은 `204a8d3`의 추적 파일과 로컬 프로젝트 모두에 없었다. 해당 참조를 가진 액터는 다음 파일이다.

[기존 화재 액터](../Content/__ExternalActors__/Maps/Prototype/4/95/9ZN4TGUM1KY0UT2A3C5GZ0.uasset)

**JJW 액터와 참조를 유지하며, 다른 시나리오의 머티리얼로 조용히 바꾸지 않는다.** 일치하는 Scenario_04 머티리얼·Sparse Volume Texture 등 의존 에셋을 팀에서 받아야 한다. 머티리얼이 없으면 런타임 경고를 출력하지만, 경고가 데이터 문제를 해결해 주는 것은 아니다.

### FDS 발화점과 데이터 정합

- FDS 발화점 원본과 Unreal 좌표·시간 매핑은 별도의 검증 자료가 필요하다.
- 바이너리 연기 파일이나 화면상 화재 위치만으로 발화점과 정합이 확정됐다고 간주하지 않는다.
- 발화점 메타데이터가 존재하는 것과 BIN/SVT 데이터 정합이 확인된 것은 서로 다른 조건이다.
- 정합 확인의 엄격한 추가 조건은 선택적 소화 접근에 적용한다. 일반 JJW 바이너리 로드·경로 탐색 API 자체의 동작은 변경하지 않았다.
- 누락되거나 확인되지 않은 경우 임의 좌표·최고 온도 셀·옛 표시 좌표로 대체하지 않는다.
- 따라서 외부 데이터가 준비되지 않은 현재 환경에서는 **소화 동작이 실제 건물에서 발생하지 않을 수 있다.** 코드 병합·자동 테스트 성공과 실제 FDS 시나리오 시연은 별도로 검증해야 한다.

상세 계약: [FDS_IGNITION_METADATA_KO.md](FDS_IGNITION_METADATA_KO.md).

## 검증 기록

기존에 보고한 35개 테스트가 그대로 유지됐다고 표현하지 않는다. 이번 병합은 JJW의 기존 테스트, 이관한 테스트, 새 통합 테스트를 함께 실행하여 결과를 다시 기록한다.

### 로컬 빌드 환경 설정

JJW의 새 한글 이름 파일을 Git이 따옴표·이스케이프 경로로 출력하면서 UBT `GitSourceFileWorkingSet`에서 `ArgumentException`이 발생했다. 이 작업 사본에는 다음 **저장소 로컬 설정**을 적용했다.

```sh
git config --local core.quotepath false
```

파일 이름이나 에셋 참조를 변경한 해결법이 아니다. 이 설정은 커밋으로 배포되지 않으므로 다른 팀원의 clone에서 같은 오류가 발생하면 별도로 확인해야 한다. 이 환경 오류 해결 자체를 C++ 컴파일 성공으로 간주하지 않는다.

| 항목 | 상태 | 결과·로그 |
|---|---|---|
| Development Editor 빌드 | 성공 | UE 5.7.4 / Win64 Development / 최종 26개 빌드 작업 성공. [빌드 로그](../../Logs/JJWMerge20260914_BuildFinal.log) |
| JJW 기존 16개 테스트 | 16/16 성공 | 원본 3개 테스트 파일은 `204a8d3`과 동일. 최종 보고서에서 16개 이름·결과 대조 |
| 이관·추가 상호작용 테스트 포함 전체 실행 | 63/63 성공 | 성공 59, 경고 포함 성공 4, 실패 0, 미실행 0. [JSON 보고서](../../Logs/JJWMerge20260914_Final/index.json), [실행 로그](../../Logs/JJWMerge20260914_Final.log) |
| 지정 Blueprint 11개 컴파일 | 성공 | 컴파일 오류 0, 컴파일 경고 0, 로드 실패 0. 프로젝트 전체가 아닌 [대상 목록](JJW_MERGE_BLUEPRINT_ALLOWLIST.txt) 범위. [로그](../../Logs/JJWMerge20260914_BlueprintsFinal.log) |
| 실제 에셋의 NPC 구성·애니메이션 | 제한 포함 확인 | 정식 RL 호환, Manny는 원래 AnimBP 보존. 보존 동작 자동 테스트 3개 통과. [에셋 검사 로그](../../Logs/JJWMerge20260914_AssetsFinal.log) |
| Prototype 편집기 맵 로드 | 부분 확인 | 로드는 성공했으나 로드된 편집기 액터 14개, NPC 0개. World Partition 전체 로드·배치·PIE 검증 아님 |
| Scenario_04 / FDS 정합 자료 | 미완료 | Scenario_04 의존 에셋 누락, FDS 메타데이터 미지정·정합 미확인. 다른 자료로 대체하지 않음 |
| 실제 화면의 배치·시작·문·소화 동작 | 미검증 | 자동 테스트만으로 완료 판정하지 않음 |

검증 시각은 2026-09-14 19:42~19:44 KST이며, 로그·JSON은 이 작업 사본의 로컬 산출물로 Git에 포함하지 않았다. 팀원은 아래 명령으로 재실행할 수 있다.

경고 포함 성공 4개는 기존 시뮬레이션 시작·완료 로그의 `Warning` 수준 출력과 합성 테스트 월드의 `DestroyActor: World has no context` 출력에 해당한다. 최종 Blueprint 명령의 엔진 전체 경고 3개(신경망 에셋 미설정, 작업 폴더 junction 경로 표기)는 Blueprint 컴파일 경고와 별개다. 권장 MSVC 버전과 설치된 버전이 다르다는 빌드 경고도 남아 있다.

에셋 Python 검사의 자체 결과는 `INCOMPLETE`(PASS 15, WARN 2, INCOMPLETE 12, FAIL 0)다. Python에서 Blueprint 컴파일 결과를 엄격히 읽지 못한 11개는 별도 컴파일 명령으로 검증했고, 나머지 1개는 실제 맵 NPC/PIE 검사 미실시다. WARN 2개는 Manny 호환 처리와 Scenario_04 누락이다. 검사 전후 에셋 2314개의 크기·수정 시각은 동일하며, 에셋 저장·리타기팅·맵 수정은 하지 않았다.

JJW 행동 상태머신·설정, 바이너리 로더 구현, HazardField, 기존 테스트 3개 파일 및 들어온 Blueprint·기존 화재 액터의 내용은 대상 커밋과 대조했다. 원본 `YUFSHazardField.h`의 마지막 빈 줄 1개는 원본 보존을 위해 유지했으며, 그 외 변경에는 `git diff --check` 공백 오류나 병합 충돌 표식이 없다.

### 검증 재실행

에디터를 종료한 상태에서 프로젝트 루트에서 실행한다. 경로는 각 팀원의 엔진·프로젝트 위치에 맞게 바꾼다. 명령줄의 `NullRHI` 검사는 화면을 렌더링하지 않는다.

```powershell
& 'C:/Program Files/Epic Games/UE_5.7/Engine/Build/BatchFiles/Build.bat' YUFSEditor Win64 Development "$PWD/YUFS/YUFS.uproject" -WaitMutex -NoHotReload -NoUBA -MaxParallelActions=2
& 'C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' "$PWD/YUFS/YUFS.uproject" -unattended -nop4 -NullRHI -NoSound -NoLiveCoding -DisablePython '-ExecCmds=Automation RunTests YUFS.' '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$PWD/Logs/JJWMerge_Rerun"
& 'C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' "$PWD/YUFS/YUFS.uproject" -run=CompileAllBlueprints '-AllowListFile=Docs/JJW_MERGE_BLUEPRINT_ALLOWLIST.txt' -unattended -nop4 -NullRHI '-ini:EditorPerProjectUserSettings:[/Script/BlueprintGraph.BlueprintEditorSettings]:SaveOnCompile=SoC_Never,[/Script/BlueprintGraph.BlueprintEditorSettings]:bSaveOnCompile=False'
& 'C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor-Cmd.exe' "$PWD/YUFS/YUFS.uproject" -run=pythonscript "-script=$PWD/YUFS/Content/Python/verify_jjw_merge_assets.py" -unattended -nop4 -NullRHI
```

### 테스트 이관 내용

- JJW 기존 지각·성격·경로·회복 16개 테스트를 기준 회귀 테스트로 보존한다.
- [YUFSInteractionNavigationTests.cpp](../Source/YUFS/Tests/YUFSInteractionNavigationTests.cpp): 오래된 256개 패치 주입과 별도 물리 막힘 예산 대신 JJW 콜백, 경로 취소, 명시적 재시도, 개인 셀 기억을 검증한다.
- [UnknownHazardCognitionTests.cpp](../Source/YUFS/Tests/UnknownHazardCognitionTests.cpp): 보조 인지 테스트는 유지하되 제거된 BSM 위험도 강제 설정·옛 회복 규칙의 가정을 새 상태머신에 억지로 요구하지 않는다. 데이터 누락·로딩·실제 맑음의 구분과 관측을 통한 안내 전달을 검증한다.
- [JJWInteractionAdapterTests.cpp](../Source/YUFS/Tests/JJWInteractionAdapterTests.cpp): 데이터 미확인 상태의 선택적 접근 금지, 요청 세대와 지각 기억 초기화를 검증한다.
- [JJWControllerMergeTests.cpp](../Source/YUFS/Tests/JJWControllerMergeTests.cpp): 수동 배치 유지, 자동 전시 비활성화, 전체 인원 중복 방지, 중도 삭제 후 완료 판정을 검증한다.
- [SuppressionAttemptSafetyTests.cpp](../Source/YUFS/Tests/SuppressionAttemptSafetyTests.cpp): JJW 상태와 소화 시도 조건의 관계를 검증한다.
- [JJWDecisionBridgeTests.cpp](../Source/YUFS/Tests/JJWDecisionBridgeTests.cpp): 실제 관측으로 준비·긴급 대피·도움 상태에 진입시켜, 예전 의도 쓰기·연출 작업 완료·2초 행동 유지가 JJW 결정을 덮어쓰지 않는지 검사한다.
- [JJWAnimationFallbackTests.cpp](../Source/YUFS/Tests/JJWAnimationFallbackTests.cpp): 실제 RL·Manny 에셋을 로드하여 정상 재생, 원래 AnimBP 보존, 사용자 지정 바인딩 검사를 검증한다.
- [InteractionContinuityTests.cpp](../Source/YUFS/Tests/InteractionContinuityTests.cpp): 자료가 없다는 이유만으로 무조건 `Shelter`를 강제하던 이전 기대값을 제거하고, 도움 종료 후에도 JJW가 실제로 결정한 의도가 유지되는지 확인한다.

테스트에 사용하는 합성 격자·월드는 제어된 입력이다. 실제 건물의 NavMesh, 벽·문 배치, FDS 변환 정확성을 입증하는 자료가 아니다.

### 실제 화면 인수 확인 항목

- NPC 배치·회전·취소·삭제 후 인원 수가 맞고, 기다려도 배치가 자동으로 바뀌지 않는다.
- 시작 버튼 전에는 임의 행동 전시·카메라 이동·자동 시뮬레이션 시작이 없다.
- Pause/Resume/Stop/타임라인 관찰이 NPC·문·소화 동작과 충돌하지 않는다.
- 위험 인지 전후의 개인별 반응과 대피 준비가 JJW 동작을 유지한다.
- 문을 연 같은 NPC가 계속 통과·대피하고 문 액터가 유지된다.
- 데이터가 검증된 경우에만 소화 선택을 시연한다. 위험 상승 시 중단하며 기록된 화재 값은 변하지 않는다.
- Scenario_04 에셋 누락과 FDS 정합 미확인은 별도의 미완료 조건으로 기록한다.

## 보존한 사용자 파일

작업 시작 전에 있던 `Content/Python/review_existing_building.py`, `Content/Python/review_local_fire.py`는 사용자의 미추적 파일이다. 이번 병합 대상으로 자동 추가하지 않으며 내용을 임의로 교체하지 않는다.

최종 커밋·원격 푸시 여부는 별도로 기록한다. 백업 브랜치가 있다는 이유로 작업 디렉터리를 강제 초기화하거나 사용자 파일을 지워서는 안 된다.
