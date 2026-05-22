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
