# YUFS RL 모델 입출력 명세

RF(Random Forest) 및 BC(Behavior Cloning MLP) 두 모델은 동일한 관측 스키마와 행동 공간을 공유한다.

---

## 공통 스키마

### 입력 벡터 — 관측(Observation)

**차원: 28 (`OBSERVATION_DIM`)**  
`schema.py`의 `OBSERVATION_FIELDS`에서 정의. 모든 값은 float32.

| idx | 필드명 | 범위 / 단위 | 설명 |
|-----|--------|-------------|------|
| 0 | `smoke_density_at_self` | [0, 1] | NPC 위치의 정규화된 연기 밀도 |
| 1 | `temperature_at_self` | [0, 1] | NPC 위치의 정규화된 온도 |
| 2 | `smoke_in_front_normalized` | [0, 1] | NPC 전방에서 감지된 최대 연기 수준 |
| 3 | `smoke_above_normalized` | [0, 1] | NPC 상방에서 감지된 최대 연기 수준 |
| 4 | `risk_level` | [0, 1] | Perception 컴포넌트의 위험도 추정값 |
| 5 | `sim_time_normalized` | [0, 1] | 시뮬레이션 시간 (화재 프레임 범위로 정규화) |
| 6 | `dist_to_nearest_exit_normalized` | [0, 1] | 가장 가까운 안전 출구까지의 거리 ÷ 10000 cm |
| 7 | `dist_to_familiar_exit_normalized` | [0, 1] | 친숙한 출구까지의 거리 ÷ 10000 cm |
| 8 | `dir_to_nearest_exit_x` | [-1, 1] | 가장 가까운 출구 방향 벡터 X |
| 9 | `dir_to_nearest_exit_y` | [-1, 1] | 가장 가까운 출구 방향 벡터 Y |
| 10 | `dir_to_nearest_exit_z` | [-1, 1] | 가장 가까운 출구 방향 벡터 Z |
| 11 | `nearest_exit_smoke_free` | {0, 1} | 가장 가까운 출구가 위험하지 않으면 1 |
| 12 | `nearby_evacuating_ratio` | [0, 1] | 주변 NPC 중 현재 대피 중인 비율 |
| 13 | `nearby_npc_count_normalized` | [0, 1] | 주변 NPC 수 ÷ 20 |
| 14 | `group_size_normalized` | [0, 1] | 자신 포함 그룹 크기 ÷ 20 |
| 15 | `nearby_npc_needs_help` | {0, 1} | 주변 맥락상 도움이 필요한 NPC가 있으면 1 |
| 16 | `alarm_sounding` | {0, 1} | NPC에게 화재 경보가 전달되었으면 1 |
| 17 | `received_pre_recorded_message` | {0, 1} | 사전 녹음된 비상 메시지 수신 여부 |
| 18 | `received_live_announcement` | {0, 1} | 실시간 비상 방송 수신 여부 |
| 19 | `received_staff_guidance` | {0, 1} | 직원 유도 수신 여부 |
| 20 | `staff_guided_exit_x_normalized` | [0, 1] | 직원이 안내한 출구 목표 X 좌표 (정규화) |
| 21 | `staff_guided_exit_y_normalized` | [0, 1] | 직원이 안내한 출구 목표 Y 좌표 (정규화) |
| 22 | `staff_guided_exit_z_normalized` | [0, 1] | 직원이 안내한 출구 목표 Z 좌표 (정규화) |
| 23 | `current_state_normalized` | [0, 1] | 현재 행동 상태 enum ÷ 최대 enum 값 |
| 24 | `risk_perception` | [0, 1] | 상태 머신의 위험 인식값 |
| 25 | `stress_level` | [0, 1] | 스트레스 수준 (perception risk level과 동일) |
| 26 | `milling_action_count_normalized` | [0, 1] | Milling 행동 횟수 ÷ 20 |
| 27 | `smoke_exposure_accumulated` | [0, 1] | 누적 연기 노출량 |

### 출력 클래스 — 행동(Action)

**클래스 수: 11**  
모델의 추론 결과는 아래 `ActionId` 중 하나의 정수 인덱스.

| ID | 행동명 | 설명 |
|----|--------|------|
| 0 | `Idle` | 대기 |
| 1 | `SeekInformation` | 정보 탐색 |
| 2 | `AlertNearbyOccupants` | 주변 재실자에게 경고 |
| 3 | `GatherBelongings` | 소지품 챙기기 |
| 4 | `EvacuateToNearestExit` | 가장 가까운 출구로 대피 |
| 5 | `EvacuateToFamiliarExit` | 친숙한 출구로 대피 |
| 6 | `HelpOther` | 타인 돕기 |
| 7 | `WaitForInfo` | 정보 대기 |
| 8 | `Cough` | 기침 |
| 9 | `FollowCrowd` | 군중 따르기 |
| 10 | `Film` | 촬영 |

---

## RF 모델 (Random Forest Baseline)

### 구조

- 알고리즘: `sklearn.ensemble.RandomForestClassifier`
- 기본 설정: 트리 200개, 최대 깊이 20, `class_weight="balanced"`
- 저장 형식: `.pkl` (pickle)

### 입력

```
observation: float32[batch, 28]
```

- UE5 시뮬레이션이 CSV에 기록한 `state` 컬럼 값을 그대로 사용
- **별도 정규화 없음** — scikit-learn RF는 스케일 불변

### 추론

```
clf.predict(X)        → int64[batch]          # 행동 ID (argmax)
clf.predict_proba(X)  → float32[batch, n_seen] # 클래스별 확률
```

### ONNX 출력 (`export_onnx_rf.py`)

```
action_logits: float32[batch, 11]
```

RF가 학습 중 본 클래스가 12개 미만일 경우, `append_class_expansion()`이  
`[batch, n_seen]` 확률 행렬에 치환 행렬 P를 MatMul하여 전체 12차원으로 확장한다.

```
P[i, classes[i]] = 1.0   # shape: [n_seen, 12]
action_logits = probs @ P
```

Unreal NNE/ORT에서 `action_logits`의 argmax를 `EYUFSAction` enum 인덱스로 직접 사용.

---

## BC 모델 (Behavior Cloning MLP)

### 구조

- 알고리즘: PyTorch MLP (`BCMLPClassifier`)
- 기본 아키텍처: Linear(28→128) → ReLU → Linear(128→128) → ReLU → Linear(128→11)
- 손실 함수: CrossEntropyLoss (역빈도 기반 클래스 가중치 적용)
- 옵티마이저: AdamW (lr=1e-3, weight_decay=1e-5)
- 저장 형식: `.pt` (PyTorch checkpoint)

### 입력

```
observation: float32[batch, 28]
```

학습 시 훈련 세트 기준으로 z-score 정규화를 적용한다.

```python
normalized = (observation - input_mean) / input_std
```

- `input_mean`, `input_std`는 체크포인트(`.pt`)에 함께 저장됨
- 표준편차가 1e-6 미만인 피처는 std=1.0으로 처리 (0 나눗셈 방지)

### 추론

```python
logits  = model.forward(normalized_obs)   # float32[batch, 12]  — raw logit
action  = model.predict(normalized_obs)   # int64[batch]         — argmax
```

### ONNX 출력 (`export_onnx.py`)

```
입력:  observation   — float32[batch, 28]  (정규화 전 원시값)
출력:  action_logits — float32[batch, 11]  (raw logit)
```

`NormalizedPolicyWrapper`가 ONNX 그래프 내부에서 정규화를 처리하므로,  
Unreal 측에서는 원시 관측 벡터를 그대로 입력하면 된다.

---

## 데이터 파이프라인 요약

```
UE5 시뮬레이션
    │  (매 결정 스텝마다 CSV 행 기록)
    ▼
Saved/RLTransitions/*.csv
    │  컬럼: run_id, agent_id, step_index, sim_frame,
    │         sim_time_seconds, action,
    │         done, terminal_reason, state, next_state
    │
    ├─ train_rf.py  ──→ rf_model.pkl  ──→ export_onnx_rf.py  ──→ rf_model.onnx
    └─ train_bc.py  ──→ bc_model.pt   ──→ export_onnx.py     ──→ bc_model.onnx
                                                                       │
                                                                 Unreal NNE/ORT
                                                                 (argmax → EYUFSAction)
```

### 에피소드 종료 조건 (`terminal_reason`)

| 값 | 의미 |
|----|------|
| `None` | 에피소드 진행 중 |
| `ReachedExit` | 출구 도달 (성공) |
| `Incapacitated` | 부상/실신 (실패) |
| `TimedOut` | 시간 초과 |