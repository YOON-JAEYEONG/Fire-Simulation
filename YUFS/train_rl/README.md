# YUFS RL Training Helpers

This folder turns Unreal transition logs into a Python-friendly dataset.

## Log source

Transition CSV files are written by Unreal to:

```text
Saved/RLTransitions/
```

Each row contains:

```text
run_id,agent_id,step_index,sim_frame,sim_time_seconds,
action,reward,done,terminal_reason,state,next_state
```

`state` and `next_state` are semicolon-delimited vectors produced by `FYUFSNPCObservation::ToFloatArray()`.

## Files

- `schema.py`: fixed observation index map and action/terminal ids.
- `dataset.py`: CSV parser and helpers for loading transitions.
- `inspect_dataset.py`: quick dataset sanity check from the command line.
- `bc_model.py`: torch MLP used for the behavior cloning baseline.
- `train_bc.py`: trains a behavior cloning policy from transition logs and saves weights plus metadata.
- `train_rf.py`: trains a Random Forest baseline from transition logs.
- `export_onnx.py`: exports a `bc_model.pt` checkpoint to ONNX (includes input normalization).
- `export_onnx_rf.py`: exports a `rf_model.pkl` checkpoint to ONNX (expands output to full 12-class space).

## Quick start

Inspect the latest logs:

```powershell
python train_rl/inspect_dataset.py
```

Inspect a specific CSV file:

```powershell
python train_rl/inspect_dataset.py Saved/RLTransitions/yufs_rl_transitions_20260516_230000.csv
```

Use from Python:

```python
from train_rl.dataset import load_transition_directory, transitions_to_numpy_dict

transitions = load_transition_directory("Saved/RLTransitions")
batch = transitions_to_numpy_dict(transitions)
print(batch["state"].shape)
```

## Behavior cloning baseline

Train a first policy from logged state-action pairs:

```powershell
.\.venv\Scripts\python.exe train_rl/train_bc.py --epochs 30 --batch-size 1024
```

This saves artifacts under:

```text
Saved/RLModels/bc_baseline_YYYYMMDD_HHMMSS/
```

Files:

- `bc_model.pt`: torch checkpoint with state dict, config, and input normalization stats.
- `metadata.json`: training metrics, class weights, per-action validation accuracy, and observation schema.

## Export to ONNX for Unreal

Export the trained checkpoint into a single ONNX file that already includes input normalization:

```powershell
.\.venv\Scripts\python.exe train_rl/export_onnx.py `
  --checkpoint Saved/RLModels/bc_baseline_YYYYMMDD_HHMMSS/bc_model.pt `
  --output Saved/RLModels/bc_baseline_YYYYMMDD_HHMMSS/bc_model.onnx
```

The exported model expects a raw `29`-dim observation vector and returns action logits.

## Random Forest baseline

Random Forest은 특징 정규화가 불필요하고 학습이 빠릅니다. 데이터셋이 클 경우 `--max-samples`로 샘플 수를 제한하세요.

### 학습

```powershell
.\.venv\Scripts\python.exe -m train_rl.train_rf `
  --data        Saved/RLTransitions `
  --output-dir  Saved/RLModels `
  --max-samples 500000 `
  --n-estimators 200 `
  --max-depth   20
```

주요 옵션:

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `--max-samples` | `500000` | 학습에 사용할 최대 트랜지션 수 (0 = 전체) |
| `--n-estimators` | `200` | 트리 개수 |
| `--max-depth` | `20` | 트리 최대 깊이 (0 = 무제한) |
| `--n-jobs` | `-1` | 병렬 코어 수 (-1 = 전체) |
| `--val-ratio` | `0.2` | 검증 분할 비율 |

결과물은 아래에 저장됩니다:

```text
Saved/RLModels/rf_baseline_YYYYMMDD_HHMMSS/
  rf_model.pkl    — sklearn 체크포인트
  metadata.json   — 학습 메트릭 및 클래스별 정확도
```

### ONNX 내보내기

```powershell
.\.venv\Scripts\python.exe -m train_rl.export_onnx_rf `
  --checkpoint Saved/RLModels/rf_baseline_YYYYMMDD_HHMMSS/rf_model.pkl `
  --output     Saved/RLModels/rf_baseline_YYYYMMDD_HHMMSS/rf_model.onnx
```

내보낸 모델의 입출력:

- 입력 `observation`: `float32 [batch, 29]` — 원시 관측 벡터 (정규화 불필요)
- 출력 `action_logits`: `float32 [batch, 12]` — 전체 액션 공간의 확률값

학습 데이터에 등장하지 않은 액션(예: `HelpOther`, `WaitForInfo`, `Cough`)은 출력 확률이 0으로 채워집니다.
`YUFSRLPolicy`는 argmax로 액션을 선택하므로 MLP ONNX와 동일하게 동작합니다.

## Unreal inference hookup

In `AYUFSEvacuationNPC`:

- set `PolicyType` to `RL`
- set `RLModelPath` to the exported `.onnx` file path
- optionally set `RLRuntimeName`
  - `NNERuntimeORTCpu`: CPU inference
  - `NNERuntimeORTDml`: DirectML-backed GPU inference on Windows

Relative paths are resolved from the project directory, so a value like:

```text
Saved/RLModels/bc_baseline_YYYYMMDD_HHMMSS/bc_model.onnx
```

works directly.
