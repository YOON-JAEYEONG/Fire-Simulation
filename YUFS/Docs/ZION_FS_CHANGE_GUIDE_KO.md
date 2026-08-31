# `zion-fs` 브랜치 변경 소개

## 한 줄 요약

`IT_YOON_JAEYEONG` 브랜치의 `1189e6e`를 기준으로, 화재 대피 NPC에 **근거 기반 의사결정 계층**, **결정 재현·추적 기능**, **건물 내부 1·2층 자동 분산 배치**, **11개 행동 애니메이션과 검수 갤러리**를 추가했다. 기존 `EYUFSAction` 11개와 ONNX 입력 28개 계약은 유지한다.

## 팀원이 먼저 볼 핵심 변화

| 영역 | 기존 | 변경 후 |
|---|---|---|
| 의사결정 | 관측값에서 행동 정책으로 바로 연결 | `Observation → Belief → Intent → ActionTask → Action/Navigation`으로 책임 분리 |
| 확률 처리 | 주기적 재판정 시 누적 확률 편향 가능 | 새 단서가 생긴 `APPRAISE` 시점에만 1회 추첨 |
| 대피 전 행동 | 행동 종류와 지속시간의 명시적 수명주기 부족 | 정보 탐색·일상 지속·물품 회수·도움·안내 대기·촬영을 시작/완료/취소 가능한 태스크로 관리 |
| 경로 안전 | 친숙도·군중·리더 신호가 위험 출구로 연결될 여지 | 위험 출구를 먼저 마스킹하고, 안전 후보가 없으면 `Shelter`로 전환 |
| 난수 | 기능 간 난수 소비 순서가 결과에 영향 가능 | NPC별 4개 독립 난수 스트림으로 같은 seed의 결과 재현 |
| 사후 분석 | 최종 행동 중심 | 결정 시점, 확률, 근거, 태스크 전이, 난수 draw 수를 JSONL로 기록 |
| 초기 NPC 위치 | 동일 지점에 겹치거나 외부 지형으로 이동 가능 | CAD 건물의 정적 바닥을 감지해 1·2층에 균등 배치하고 최종 위치를 다시 실내 검증 |
| 행동 시각화 | 일부 기침·촬영 몽타주 슬롯만 존재 | 11개 행동과 기어가기·행동불능 상태를 스켈레톤 호환 애니메이션에 연결하고 대기 화면에서 전부 미리보기 |
| Windows 실행 | 한글 경로에서 `cl.exe` 시작 오류 가능 | 영문 junction을 거치는 안전 빌드·실행 스크립트 제공 |

## 1. NPC 결정 흐름

```text
Perception / Observation
          │
          ▼
Belief: 단서를 log-odds로 결합해 pCommit 계산
          │
          ▼
Intent: Observe / Prepare / CommitEvac / Help / Shelter /
        Reenter / Incapacitated
          │
          ▼
ActionTask: 시간 제한 대피 전 행동의 시작·완료·취소
          │
          ▼
기존 EYUFSAction 11개 / Navigation / Animation
```

이 구조는 기존 Blueprint 및 ONNX 계약을 깨지 않고 중간 판단 상태를 추가하기 위한 것이다. 주요 연결 지점은 다음과 같다.

- `YUFSEvacuationNPC`: 관측 갱신, belief/intent/task 실행, 결정 로그 기록을 조정한다.
- `YUFSBeliefComponent`: 경보·연기·열·공식 안내·군중 등 단서를 대피 확률로 결합한다.
- `YUFSIntentComponent`: 확률 gate와 안전 조건을 명시적 intent로 변환한다.
- `YUFSActionTaskComponent`: 대피 전 행동의 지속시간과 중단 사유를 관리한다.
- `YUFSBehaviorStateMachine`: intent를 기존 PADM 상태에 투영한다.
- `YUFSRuleBasedPolicy` / `YUFSOnnxPolicy`: NPC별 결정 난수 소스를 주입받는다.

### 확률을 상태로 연결하는 규칙

1. 비상 단서가 없으면 `p=.05`를 매 tick 반복 추첨하지 않고 `Observe`를 유지한다.
2. 경보·연기처럼 새 단서가 생긴 `APPRAISE` 시점에만 `random < pCommit`을 한 번 평가한다.
3. 실패 시 대피 전 행동 목표 횟수를 `1–5회 88.5%`, `6–9회 8.1%`, `10–15회 3.4%` 구간에서 뽑는다.
4. 행동 하나가 끝났을 때만 다음 appraisal을 요청한다.
5. 목표 횟수 완료, 생명 위험, 검증된 공식 지시는 확률 gate를 종료하고 `CommitEvac`으로 전환한다.
6. 안전 출구가 하나도 없으면 이동을 강행하지 않고 `Shelter`로 전환한다.

이 방식은 같은 확률을 매초 재추첨해 장기 대피율이 사실상 100%로 수렴하는 누적 Bernoulli 편향을 막는다.

### 현재 적용한 belief 값

| 입력 단서 | 기본값/결합값 | 구현 의미 |
|---|---:|---|
| 비상 단서 없음 | 0.05 | 추첨 없이 `Observe` 유지 |
| 경보 | 0.25 | 최초 appraisal 기준확률 |
| 확인된 연기 | 0.65 | 강한 대피 단서 |
| 고열·고농도 연기 | 0.90 | 생명 위험 단서 |
| 훈련 경험 | LR 1.4 | odds에 곱함 |
| 공식 안내·리더 대리지표 | LR 2.2 | odds에 곱함 |
| 이동 군중 | LR 1.5 | odds에 곱함 |
| 검증된 공식 지시 | 1.0에 준함 | 확률 gate 우회 |

이 숫자는 전체 인구의 최종 행동 비율이 아니라, 특정 단서가 들어온 **결정 시점의 조건부 파라미터**다. 시나리오별 인구 비율로 오해하지 않도록 주의한다.

## 2. 대피 전 행동 태스크

| 태스크 | 시간 모델 | 중단 조건 |
|---|---|---|
| 정보 탐색 | LogNormal, median 14초, sigma .45, 최대 45초 | 생명 위험·공식 지시·intent 변경 |
| 관찰/공식 안내 대기 | LogNormal, median 35초, sigma .55, 최대 120초 | 동일 |
| 물품 회수 | LogNormal, median 20초, sigma .45, 최대 60초 | 동일 |
| 경고/도움 | LogNormal, median 28초, sigma .55, 최대 120초 | 동일 |
| 초기 소화 | Triangular 15/20/30초 | 시간 모델만 예약 |
| 순간 경직 | Uniform 1–3초 | 시간 모델만 예약 |

초기 소화와 경직은 프로젝트에 소화기/발화점 상호작용 및 경직 애니메이션 API가 없어 아직 실제 `Action`으로 시작하지 않는다.

## 3. 건물 내부 1·2층 NPC 분산

초기 위치가 겹친 NPC 묶음을 stable NPC ID 순서로 처리하고, CAD 건물의 넓고 얇은 `StaticMesh` 상면을 층 후보로 감지한다.

```text
겹친 NPC 군집 탐색
  → 정적 바닥 높이 군집화
  → stable ID 기준 1F/2F round-robin 할당
  → golden-angle 후보 생성
  → NavMesh로 XY 보정
  → 정적 바닥 + 천장 또는 사방 벽으로 실내 판정
  → FindTeleportSpot
  → 이동 결과를 다시 실내·층·간격 검증
  → 캡슐 중심을 실제 바닥 높이에 snap
```

핵심 안전장치는 다음과 같다.

- `Landscape`를 바닥으로 인정하지 않아 건물 외부 배치를 차단한다.
- 1차로 천장 충돌이 확인된 위치를 사용하고, CAD 천장 충돌이 없는 구역은 동·서·남·북 사방 벽 판정으로 폴백한다.
- `FindTeleportSpot`이 위치를 외부로 보정할 수 있으므로 반환 위치를 신뢰하지 않고 다시 검증한다.
- 층별로 최소 간격을 검사해 서로 다른 층의 같은 XY는 허용하되 같은 층의 겹침은 방지한다.
- 감지된 층을 stable ID 기반 round-robin으로 배정해 실행 순서와 무관하게 균등 분배한다.

기본 튜닝값은 2개 층, 간격 300 cm, 최대 반경 3000 cm, NPC당 최대 256개 후보, 층 높이 오차 100 cm다. 맵 구조가 달라지면 `Simulation|NPC Distribution` 속성에서 조정한다.

### 실제 맵 검증 결과

2026-08-31 자동 PIE에서 현재 맵의 바닥 높이를 `1F=1.0 cm`, `2F=363.5 cm`로 감지했다. 20명을 1층 10명, 2층 10명으로 모두 이동했으며 같은 층 간격은 300 cm 설정을 사용했다.

```text
[YUFS] Indoor floor levels detected: 1F=1.0cm, 2F=363.5cm.
[YUFS] NPC spawn distribution complete: 20/20 moved,
       floors [1F=10, 2F=10], spacing 300 cm,
       ceiling-validated 10, wall-enclosure fallback 10,
       nav projections 2478, floor hits 128, collision rejections 0.
```

검증 로그는 로컬 `YUFS/Saved/Logs/NPCTwoFloorDistribution.log`에 생성되며 `Saved`는 Git에 포함하지 않는다.

## 4. 재현성과 결정 추적

`(scenarioSeed, stableNpcId)`에서 아래 난수 스트림을 독립 생성한다.

- `decision`: 대피 commit과 의도 판단
- `taskDuration`: 행동 지속시간
- `route`: 경로 선택
- `social`: 사회적 영향

따라서 태스크 시간이 난수를 한 번 더 소비해도 경로 난수열은 변하지 않는다. 결정 전이는 `Saved/DecisionTraces/yufs_decisions_*.jsonl`에 기록한다.

각 행에는 run/NPC/tick/시뮬레이션 시간, policy·scenario hash, decision index, 이전·다음 intent, trigger, `pCommit`, cue mask, task와 취소 사유, 대피 전 행동 진행 수, 네 난수 스트림의 draw counter, 근거 source ID가 포함된다.

## 4-1. 행동 애니메이션과 검수 화면

`UYUFSActionAnimationComponent`가 11개 `EYUFSAction`을 현재 NPC의 `Crawling__1__Skeleton`과 호환되는 `/Game/NPCs` 애니메이션에 연결한다. 시뮬레이션 시작 전에는 20명의 NPC가 11개 행동을 나눠 전시하며 머리 위에 `Action`과 실제 `Anim` 에셋 이름이 표시된다. 시작 버튼을 누르면 미리보기는 자동 해제되고 AI가 선택한 실제 행동에 맞춰 전환된다.

전체 매핑과 확인법은 `Docs/NPC_ACTION_ANIMATION_GUIDE_KO.md`를 참고한다.

## 5. 변경 파일 안내

### 새 핵심 파일

- `Source/YUFS/Core/YUFSDeterministicRng.*`: 독립 난수 스트림
- `Source/YUFS/Core/YUFSDecisionTraceLogger.*`: thread-safe JSONL 결정 로그
- `Source/YUFS/NPC/Decision/YUFSBeliefComponent.*`: 단서 기반 belief 계산
- `Source/YUFS/NPC/Decision/YUFSIntentComponent.*`: 확률을 intent 상태로 연결
- `Source/YUFS/NPC/Tasks/YUFSActionTaskComponent.*`: 시간 제한 태스크 수명주기
- `Source/YUFS/NPC/Animation/YUFSActionAnimationComponent.*`: 행동·상태 애니메이션 매핑과 스켈레톤 검증
- `Source/YUFS/Tests/YUFSDecisionModelTests.cpp`: 결정 모델 4개와 애니메이션 호환성 테스트 1개

### 주요 수정 파일

- `Source/YUFS/Core/YUFSTypes.h`: intent/task/cancel/terminal enum 추가
- `Source/YUFS/NPC/YUFSEvacuationNPC.*`: 새 결정 파이프라인 조정 및 분산 위치 적용
- `Source/YUFS/NPC/Behavior/YUFSBehaviorStateMachine.*`: intent의 PADM 상태 투영
- `Source/YUFS/NPC/Decision/YUFSRuleBasedPolicy.*`, `YUFSOnnxPolicy.h`: 결정 난수 주입
- `Source/YUFS/NPC/Social/YUFSSocialInfluenceComponent.cpp`: 결정 단서 연결 보완
- `Source/YUFS/Level/YUFSLevelDataManager.*`: 출구 위험/권고/군중 목적지 안전 정보 제공
- `Source/YUFS/Simulation/YUFSSimulationController.*`: 실내 다층 NPC 분산
- `Source/YUFS/Simulation/YUFSTimelineTypes.h`: intent/task/terminal 상태 기록 확장

### 실행 보조 파일

- `Launch-YUFS-Safe.cmd`: 빌드 없이 안전 경로로 Editor 실행
- `Build-And-Launch-YUFS-Safe.cmd`: Editor 종료 확인 → Development 빌드 → 실행

두 스크립트는 Unreal Engine 5.7 기본 설치 경로와 `C:\CodexWork\FireSimulationWorkspace` junction을 사용한다. UE 설치 위치가 다르면 스크립트 상단의 `YUFS_EDITOR`/`YUFS_BUILD`를 수정한다.

## 6. 실행과 검증

### 빠른 실행

1. Unreal Editor를 닫는다.
2. 최초 1회 `Build-And-Launch-YUFS-Safe.cmd`를 실행한다.
3. 이후 코드 재빌드가 필요 없으면 `Launch-YUFS-Safe.cmd`를 사용한다.

한글이 포함된 OneDrive 경로를 MSVC가 처리할 때 발생하던 `cl.exe (0xc0000142)` 시작 오류를 피하기 위해 영문 junction 경로로 빌드·실행한다.

### 검증 명령

```bat
"C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" ^
  YUFSEditor Win64 Development ^
  C:\CodexWork\FireSimulationWorkspace\YUFS\YUFS.uproject ^
  -WaitMutex -NoHotReload -NoUBA
```

자동화 테스트 묶음은 `YUFS.NPC.Decision`이며 다음을 확인한다.

- 동일 seed/ID 난수열과 스트림 독립성
- 경보·연기·공식 지시의 `pCommit`
- 단서가 없을 때 대피하지 않음과 tick 재추첨 금지
- 태스크 시작·완료·교체 이벤트와 완료 태스크 재시작 방지

### 이 브랜치의 최종 검증 상태

- `YUFSEditor Win64 Development`: 성공
- `YUFS.NPC`: 결정 4개 + 애니메이션 매핑·에셋·스켈레톤 호환성 1개 성공, 경고 0, 실패 0
- Editor PIE 실내 다층 배치: 20/20 이동, 1층 10명, 2층 10명
- Editor PIE 행동 갤러리: 20명에게 11개 행동 전부 배정, 누락 에셋·스켈레톤 오류 0
- `git diff --check`: 공백 오류 없음

자동화 보고서는 로컬 `YUFS/Saved/Automation/ZionFS/index.json`에 생성되며 Git에는 포함하지 않는다.

## 7. 근거와 구현 연결

구현 상세 수치는 `Docs/NPC_DECISION_IMPLEMENTATION.md`에 요약돼 있다. 원 조사 자료와 통합 설계 문서에서 사용한 source ID는 다음과 같다.

| source ID | 반영한 부분 |
|---|---|
| `B:initial` | 경보와 행동 share의 초기 초안 |
| `D:p8-10` | 고열·연기 및 생명 위험 조건 |
| `E:p23-26` | 대피 개시와 대피 전 행동 횟수 |
| `E:p47-50` | 물품 회수 및 정보 탐색 |
| `E:p68-73` | 그룹·리더·도움 행동 |
| `PROJECT:decision-draft` | 재현성, 인터럽트, 안전 후보 0개 처리 등 공학 규칙 |

원자료 파일명은 `FRN-0953.pdf`, `odpm_fire_033353.pdf`, `s10694-026-01928-w.pdf`, `3.txt`, `화재대피_NPC_행동비율_근거브리프.html`이며, 해석·통합 기준은 `fire-evacuation-npc-probability-state-spec.html`이다. 저작권 및 용량 때문에 원 PDF는 이 브랜치에 복제하지 않았다.

## 8. 알려진 제한과 후속 작업

- 현재 저장소에는 `Content/Fires/Scenario_01/smoke_data.bin`이 없어 PIE 로그에 화재 데이터 로드 오류가 발생한다. NPC 다층 분산은 검증됐지만 실제 연기 시뮬레이션을 검증하려면 해당 바이너리를 동기화하거나 생성해야 한다.
- 초기 소화와 순간 경직은 시간 모델만 있고 월드 상호작용/애니메이션이 아직 없다.
- 모든 대피 전 행동의 masked-softmax, 소화기 자원 예약, 전체 출구 그래프 효용 계산은 후속 범위다.
- 도움 대상 단일 예약, 그룹 hysteresis, 전역 propose/resolve/commit 배리어는 아직 구현하지 않았다.
- 명시적 보호 대상과 권한을 포함한 재진입 실행은 아직 구현하지 않았다.
- 이 맵의 다층 검증은 Editor PIE 기준이다. 별도 `-game` headless 경로는 World Partition 초기화 차이를 추가 확인해야 한다.

## 리뷰 권장 순서

1. `NPC_DECISION_IMPLEMENTATION.md`에서 모델 의도와 수치를 확인한다.
2. `YUFSIntentComponent.cpp`에서 확률 gate가 tick마다 재추첨되지 않는지 본다.
3. `YUFSEvacuationNPC.cpp`에서 기존 행동 파이프라인과의 연결을 본다.
4. `YUFSSimulationController.cpp`에서 실내·층·간격의 최종 재검증을 본다.
5. `YUFSDecisionModelTests.cpp`를 실행하고 decision trace를 확인한다.
6. 누락된 `smoke_data.bin`을 받은 뒤 실제 화재 시나리오 회귀 테스트를 진행한다.
