# 화재 대피 NPC 결정 모델 구현 메모

## 적용 범위

현재 구현은 기존 `EYUFSAction` 11개와 ONNX 입력 28개 계약을 변경하지 않고, 그 위에 근거 기반 결정 계층을 추가한 실행 가능한 핵심 범위다.

`Observation → Belief(log-odds) → Intent → ActionTask → 기존 Action/Navigation`

- `Belief`: 경보 `.25`, 연기 확인 `.65`, 고열·고농도 연기 `.90`을 기준확률로 사용한다. 훈련 `LR=1.4`, 안내·리더 대리지표 `LR=2.2`, 이동 군중 `LR=1.5`를 odds에 곱한다.
- `Intent`: `Observe`, `Prepare`, `CommitEvac`, `Help`, `Shelter`, `Reenter`, `Incapacitated`를 기존 PADM 상태에 투영한다.
- `ActionTask`: 정보 탐색·일상 지속·물품 회수·도움·공식 안내 대기·촬영을 시간 제한 작업으로 관리하며, 생명 위험·공식 지시·의도 변경 시 즉시 취소한다.
- `Route safety`: 위험한 익숙한 출구·직원 권고 출구·군중 목적지를 후보에서 제거한다. 안전 후보가 0개면 `Shelter`, 새 경로가 생기면 `CommitEvac`으로 돌아간다.
- `Determinism`: `(scenarioSeed, stableNpcId)`에서 `decision`, `taskDuration`, `route`, `social` 난수 스트림을 독립적으로 만든다.

## 확률을 상태로 연결하는 규칙

1. 실제 비상 cue가 없으면 `.05` 기초값을 반복 추첨하지 않고 `Observe`를 유지한다.
2. cue가 새로 생긴 `APPRAISE` 시점에만 `random < pCommit`을 한 번 계산한다.
3. 실패하면 대피 전 행동 목표 횟수를 `1–5: 88.5%`, `6–9: 8.1%`, `10–15: 3.4%` band에서 뽑는다.
4. 행동 하나가 완료될 때만 다음 `APPRAISE`를 요청한다. 렌더 tick마다 재추첨하지 않는다.
5. 목표 횟수 완료, 생명 위험 또는 검증된 공식 지시는 확률 gate를 끝내고 `CommitEvac`으로 전환한다.
6. 안전 출구가 없으면 이동 행동을 실행하지 않고 `Shelter`로 전환한다.

이 규칙은 같은 `.25`를 매초 다시 뽑아 장기 대피율이 100%에 가까워지는 누적 Bernoulli 편향을 방지한다.

## 시간 분포

- 정보 탐색: `LogNormal(median=14s, sigma=.45), cap=45s`
- 관찰·공식 안내 대기: `LogNormal(median=35s, sigma=.55), cap=120s`
- 물품 회수: `LogNormal(median=20s, sigma=.45), cap=60s`
- 경고·도움: `LogNormal(median=28s, sigma=.55), cap=120s`
- 초기 소화용 예약 모델: `Triangular(15s, 20s, 30s)`
- 경직용 예약 모델: `Uniform(1s, 3s)`

초기 소화와 경직은 태스크 타입과 시간 모델까지 마련했지만, 현재 프로젝트에 소화기/발화점 상호작용 및 경직 애니메이션 API가 없어 기존 Action으로는 아직 시작하지 않는다.

## 로그와 재현

전이는 `Saved/DecisionTraces/yufs_decisions_*.jsonl`에 기록한다. 각 행에는 다음을 포함한다.

- run/NPC/tick/simulation time
- policy hash, scenario hash, decision index
- 이전·다음 intent, trigger, `pCommit`, cue mask
- task와 cancel reason, 대피 전 행동 완료/목표 횟수
- 네 RNG stream의 draw counter
- 구현값에 연결된 source ID

## 근거 연결

- `B:initial`: 프로젝트의 경보·행동 share 초안
- `D:p8-10`: 고열·연기 및 생명 위험 조건
- `E:p23-26`: 대피 개시와 대피 전 행동 횟수
- `E:p47-50`: 물품 회수 및 정보 탐색
- `E:p68-73`: 그룹·리더·도움 행동
- `PROJECT:decision-draft`: 재현성, 인터럽트, 안전 후보 0개 처리 같은 공학 규칙

세부 출처 해석은 조사 산출물 `fire-evacuation-npc-probability-state-spec.html`을 기준으로 한다. 원자료 PDF/HTML은 저작권과 저장소 용량을 고려해 이 브랜치에 복제하지 않았으며, 팀 공유 위치는 `ZION_FS_CHANGE_GUIDE_KO.md`의 근거 표를 따른다.

## 실내 다층 초기 배치

겹친 NPC는 stable NPC ID 순서로 처리하고, CAD 건물의 넓고 얇은 정적 메시 상면을 층 후보로 감지한다. 기본값은 2개 층에 round-robin으로 균등 할당하고 같은 층에서 300 cm 간격을 확보한다. `Landscape`는 바닥 후보에서 제외하며, 천장 충돌 또는 사방 벽으로 실내를 검증한다. `FindTeleportSpot` 이후에도 실내·층·간격을 다시 검사한다.

현재 맵의 자동 PIE 검증에서는 `1F=1.0 cm`, `2F=363.5 cm`를 감지했고 20명을 `1F=10`, `2F=10`으로 배치했다. 자세한 알고리즘, 실행법, 제한은 `ZION_FS_CHANGE_GUIDE_KO.md`를 참고한다.

## 검증

`YUFS.NPC.Decision` 자동화 테스트가 다음을 확인한다.

- 동일 seed/ID의 난수열 일치와 스트림 독립성
- 경보·연기·공식 지시의 `pCommit`
- 무단서 대피 금지와 tick 재추첨 금지
- 태스크 시작·완료·교체 이벤트 및 완료 태스크 재시작 방지

## 후속 구현 경계

다음 항목은 별도 월드/에셋 API가 필요해 이 범위에 포함하지 않았다.

- `evidence.json / policy.json / scenario.json` 스키마 로더와 시작 전 검증
- 모든 대피 전 행동의 masked-softmax 및 소화기 자원 예약
- 출구 그래프 전체를 대상으로 한 travel time/smoke dose/congestion/familiarity softmax
- 도움 대상 단일 예약, 그룹 합류·이탈 hysteresis, 전역 propose/resolve/commit 배리어
- 명시적 보호 대상·권한을 포함한 재진입 실행
