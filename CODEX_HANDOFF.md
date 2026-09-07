# YUFS Windows Codex 인수인계

이 문서는 macOS Codex에서 진행한 NPC 행동 구현을 Windows Codex가 이어받기 위한 기준 문서다.
작업을 시작하기 전에 이 문서와 실제 Git 상태를 함께 확인한다.

## 1. 저장소와 브랜치

- 저장소: `https://github.com/YOON-JAEYEONG/Fire-Simulation.git`
- 이어서 작업할 브랜치: `JJW_NPC_BEHAVIOR`
- NPC 행동 구현 커밋: `40f9548` (`Implement calibrated NPC evacuation decisions`)
- `main` 복구 커밋: `a408baa` (`Revert "Implement calibrated NPC evacuation decisions"`)
- 현재 구현은 `main`에서 제거되고 `JJW_NPC_BEHAVIOR`에만 보존되어 있다.
- 별도 후보 브랜치 `zion-fs`의 확인된 최신 커밋은 `c7c2da5`다. 이 브랜치에도 NPC 판단, 태스크, 애니메이션 구현이 있어 충돌 가능성이 높으므로 바로 병합하지 않는다.

Windows에서 시작할 때:

```powershell
git fetch origin --prune
git switch --track origin/JJW_NPC_BEHAVIOR
git status
git log -3 --oneline --decorate
```

이미 로컬 브랜치가 있다면 다음을 사용한다.

```powershell
git switch JJW_NPC_BEHAVIOR
git pull --ff-only origin JJW_NPC_BEHAVIOR
```

중요: 검증이 끝나기 전에는 `main`에 직접 커밋하거나 푸시하지 않는다.

## 2. 프로젝트 기준

- Unreal 프로젝트: `YUFS/YUFS.uproject`
- Unreal Engine: `5.7`
- 기본 맵: `/Game/Maps/Prototype`
- 주요 맵 액터:
  - `YUFSBinaryManager`
  - `YUFSHeterogeneousVolume`
  - `YUFSSimulationController`
- HUD: `/Game/Blueprint/WBP_SimHUD`
- NPC Blueprint: `/Game/Blueprint/BP_YUFSRLEvacuationNPC`

## 3. 구현된 NPC 행동 모델

### 위험 신호와 즉시 대피 확률

- 경보만 감지: `0.25`
- 연기 감지: `0.65`
- 불꽃 또는 고열 감지: `0.90`
- 안전교육, 공식 안내, 이동 군중 단서를 likelihood ratio로 반영한다.
- 위 값은 현재 시뮬레이션용 초기 보정값이며 개별 행동의 연구 확정 통계로 취급하지 않는다.

### 대피 전 행동

- 상황 확인: 가중치 `45`
- 기다리며 관찰: 가중치 `20`
- 소지품 챙기기: 가중치 `20`
- 주변에 알리기 또는 돕기: 가중치 `10`
- 초기 소화 시도: 가중치 `5`
- 행동별 지속시간 범위를 따로 두었다.
- 행동 개수 구간은 `1~5`, `6~9`, `10~15`이며 초기 가중치는 각각 `88.5`, `8.1`, `3.4`다.
- `Film`은 기존 액션으로 남아 있지만 이 보정된 대피 전 행동 풀에는 포함하지 않았다.

### 출구 선택

- 익숙한 출구 사전 가중치: `70`
- 군중 또는 리더 경로: `20`
- 가장 가까운 안전 출구: `10`
- 출구가 위험하면 후보에서 제외한다.
- 안전한 출구가 없으면 `ShelterInPlace`로 폴백한다.
- 이동 중 현재 목적지가 위험해지면 경로 선택을 다시 수행한다.

### 재현성과 로그

- NPC마다 `FRandomStream` 기반 시드를 사용한다.
- 같은 NPC 이름과 시드 조합은 같은 선택 흐름을 재현하도록 구성했다.
- 로그 접두사: `[YUFS][Decision]`
- 로그에 NPC, 시드, 위험 신호, 대피 확률, 난수값, 행동, 지속시간, 경로 전략과 프레임을 기록한다.

## 4. 주요 변경 파일

- `YUFS/Source/YUFS/NPC/Decision/YUFSBehaviorDecisionModel.h`
- `YUFS/Source/YUFS/NPC/Decision/YUFSBehaviorDecisionModel.cpp`
  - 확률 보정, 행동 개수, 가중 행동, 지속시간, 경로 선택을 담당하는 순수 결정 함수
- `YUFS/Source/YUFS/NPC/YUFSEvacuationNPC.h`
- `YUFS/Source/YUFS/NPC/YUFSEvacuationNPC.cpp`
  - 결정 모델을 실제 NPC 상태와 이동에 연결
- `YUFS/Source/YUFS/NPC/Behavior/YUFSBehaviorConfig.h`
  - 행동 확률, 가중치, 지속시간, 경로 사전값
- `YUFS/Source/YUFS/NPC/Behavior/YUFSBehaviorStateMachine.*`
  - 외부 결정 모델이 대피 전환을 확정할 수 있도록 연결
- `YUFS/Source/YUFS/Level/YUFSLevelDataManager.*`
  - 가장 가까운 안전 출구 탐색 API
- `YUFS/Source/YUFS/Core/YUFSTypes.h`
  - `EYUFSDangerCue`, `EYUFSRouteStrategy`, `AttemptInitialFirefighting`
- `YUFS/Source/YUFS/Core/YUFSObservation.h`
  - 관측값 기본 초기화
- `YUFS/Source/YUFS/Tests/YUFSBehaviorDecisionModelTests.cpp`
  - 결정 모델 자동화 테스트
- `YUFS/train/schema.py`, `YUFS/train/MODEL_IO_SPEC.md`
  - 액션 수를 11개에서 12개로 확장

기존 11클래스 ONNX 모델은 런타임 호환성을 유지하지만 새 액션 ID 11인 `AttemptInitialFirefighting`은 선택할 수 없다. 해당 액션을 ML 모델에서 선택하려면 12클래스로 재학습해야 한다.

## 5. 마지막 검증 기록

2026-08-31 macOS, Unreal Engine 5.7 환경에서 다음을 확인했다.

- `YUFSEditor Mac Development` 빌드 성공
- 아래 자동화 테스트 4개 성공, 종료 코드 `0`
  - `YUFS.NPC.Decision.CommitProbability`
  - `YUFS.NPC.Decision.DeterminismAndOrderInvariance`
  - `YUFS.NPC.Decision.DistributionCalibration`
  - `YUFS.NPC.Decision.SafetyFallback`

이 결과는 macOS에서의 소스 빌드와 순수 결정 로직 테스트다. Windows 빌드, 패키징, 실제 플레이 검증은 별도로 수행해야 한다.

Windows 권장 검증 순서:

1. Unreal Engine 5.7과 Visual Studio 2022의 C++ 게임 개발 도구를 설치한다.
2. 다른 OS에서 복사한 `Binaries`와 `Intermediate`를 사용하지 않고 Windows에서 다시 생성한다.
3. `YUFS.uproject`를 열고 C++ 모듈을 빌드한다.
4. `Prototype` 맵을 연다.
5. 출력 로그에서 바이너리 헤더 로드와 `[YUFS][Decision]` 로그를 확인한다.
6. 에디터 Play 후 HUD의 `Start` 버튼을 눌러야 시뮬레이션이 시작된다.
7. 경보 이후 NPC 행동, 경로 선택, 출구 도달을 확인한다.

## 6. Git에 포함되지 않는 화재 데이터

`smoke_data.bin`은 약 4.5GB이며 `.gitignore`의 `*.bin` 규칙 때문에 GitHub에 없다.

현재 확인한 파일 헤더:

```text
Frames: 8000
DimX: 153
DimY: 115
DimZ: 17
VoxelSize: 40 cm
```

Windows 프로젝트에서 반드시 아래 위치에 직접 복사한다.

```text
Fire-Simulation/YUFS/Content/Fires/FirePrototype/BinaryData/smoke_data.bin
```

정상 로그:

```text
Parsed Binary Header -> Frames: 8000, DimX: 153, DimY: 115, DimZ: 17
```

오류 로그:

```text
Failed to open binary file to read header
```

## 7. 아직 해결되지 않은 사항

1. `YUFSBinaryManager`의 월드 좌표에서 복셀 좌표로의 변환이 월드 원점을 하드코딩하고 있다.
2. 현재 데이터 기준 대략적인 유효 월드 범위는 `X (-6120, 0]`, `Y (-4600, 0]`, `Z [0, 680)`이다.
3. NPC가 이 범위 밖에 있으면 연기와 온도 샘플이 `0`으로 남을 수 있다.
4. 다른 환경에서 반복된 `NPC가 시뮬레이션 메쉬 영역 밖에 있습니다` 메시지는 현재 이 브랜치 소스의 정확한 문자열과 일치하지 않았다. 오래된 바이너리 또는 Blueprint도 확인해야 한다.
5. 경로 재선택은 구현되어 있지만 로그에 `Unsafe`, `Guidance` 같은 재선택 원인이 명시되지 않는다.
6. 현재 경로 로그는 전략만 남기므로 정확한 출구를 추적하려면 `ExitId`, 이전 목적지, 새 목적지와 재선택 원인을 추가하는 편이 좋다.
7. 디버그 컴포넌트의 `bShowPath`를 켜면 경로, 목적지 화살표와 좌표를 볼 수 있지만 기본값은 꺼져 있다.

## 8. 팀 역할 분담 시 경계

- 경로 담당: `NPC/Navigation/*`, 안전 출구와 재탐색
- 모션 담당: `NPC/Animation/*`, 현재 액션의 시각적 표현
- 상호작용 담당: `NPC/Tasks/*` 또는 새 `NPC/Interaction/*`, 소화기와 다른 NPC 등의 행동
- `YUFSEvacuationNPC.*`는 한 명의 통합 담당만 수정하는 편이 안전하다.
- 상호작용은 이동 목적지를 요청하고, 실제 이동은 경로 컴포넌트가 처리하며, 모션은 현재 액션만 표현한다.
- `.uasset`은 Git에서 자동 병합하기 어려우므로 같은 Blueprint나 맵을 여러 명이 동시에 수정하지 않는다.

## 9. Windows Codex 시작 지시문

Windows Codex에 아래 내용을 그대로 전달한다.

```text
이 저장소에서 CODEX_HANDOFF.md를 먼저 읽어라.
origin/JJW_NPC_BEHAVIOR와 커밋 40f9548을 확인하고 현재 Git 상태를 보고하라.
main에는 직접 커밋하거나 푸시하지 마라.
기존 변경을 되돌리지 말고 Unreal Engine 5.7 Windows 빌드를 먼저 검증하라.
smoke_data.bin의 실제 위치와 바이너리 헤더 로드 로그를 확인하라.
그 다음 구현된 NPC 행동 흐름, zion-fs와의 중복 가능성, 남은 문제를 요약한 뒤 작업을 시작하라.
빌드 성공, 자동화 테스트 성공, 실제 플레이 성공을 서로 구분해 보고하라.
```
