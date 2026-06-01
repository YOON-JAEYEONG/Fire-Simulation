from __future__ import annotations

import argparse
import json
from collections import Counter
from datetime import datetime
from pathlib import Path

import numpy as np
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import accuracy_score

try:
    from .dataset import load_transition_directory
    from .schema import ACTION_ID_TO_NAME, OBSERVATION_DIM
except ImportError:
    from dataset import load_transition_directory
    from schema import ACTION_ID_TO_NAME, OBSERVATION_DIM


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Train a Random Forest baseline from YUFS transition logs.")
    parser.add_argument("--data", default=str(Path("Saved") / "RLTransitions"), help="CSV file or directory of transition logs.")
    parser.add_argument("--output-dir", default=str(Path("Saved") / "RLModels"), help="Directory for trained model and metadata.")
    parser.add_argument("--max-samples", type=int, default=500000, help="Cap on total transitions (0 = no limit).")
    parser.add_argument("--val-ratio", type=float, default=0.2, help="Validation split ratio.")
    parser.add_argument("--n-estimators", type=int, default=200, help="Number of trees.")
    parser.add_argument("--max-depth", type=int, default=20, help="Max tree depth (0 = unlimited).")
    parser.add_argument("--n-jobs", type=int, default=-1, help="Parallel jobs (-1 = all cores).")
    parser.add_argument("--seed", type=int, default=42, help="Random seed.")
    return parser


def stratified_split(states: np.ndarray, actions: np.ndarray, val_ratio: float, seed: int):
    rng = np.random.default_rng(seed)
    train_idx, val_idx = [], []
    for action_id in np.unique(actions):
        idx = np.flatnonzero(actions == action_id)
        rng.shuffle(idx)
        n_val = max(1, int(round(len(idx) * val_ratio))) if len(idx) > 1 else 0
        val_idx.append(idx[:n_val])
        train_idx.append(idx[n_val:])
    return np.concatenate(train_idx), np.concatenate(val_idx)


def random_subsample(states: np.ndarray, actions: np.ndarray, max_samples: int, seed: int):
    if max_samples <= 0 or len(states) <= max_samples:
        return states, actions
    rng = np.random.default_rng(seed)
    idx = rng.choice(len(states), size=max_samples, replace=False)
    idx.sort()
    return states[idx], actions[idx]


def main() -> int:
    args = build_argument_parser().parse_args()
    output_dir = Path(args.output_dir)
    max_depth = args.max_depth if args.max_depth > 0 else None

    print("loading_data...")
    transitions = load_transition_directory(args.data)
    if not transitions:
        raise SystemExit(f"No transitions found under {args.data}")

    states = np.asarray([t.state for t in transitions], dtype=np.float32)
    actions = np.asarray([t.action_id for t in transitions], dtype=np.int64)
    print(f"loaded_transitions={len(transitions)}")

    states, actions = random_subsample(states, actions, args.max_samples, args.seed)
    print(f"sampled_transitions={len(states)}")

    unique_actions = np.unique(actions)
    print("action_coverage=" + ",".join(ACTION_ID_TO_NAME[int(a)] for a in unique_actions))

    train_idx, val_idx = stratified_split(states, actions, args.val_ratio, args.seed)
    train_x, train_y = states[train_idx], actions[train_idx]
    val_x, val_y = states[val_idx], actions[val_idx]
    print(f"train_samples={len(train_x)} val_samples={len(val_x)}")

    clf = RandomForestClassifier(
        n_estimators=args.n_estimators,
        max_depth=max_depth,
        n_jobs=args.n_jobs,
        random_state=args.seed,
        class_weight="balanced",
        verbose=1,
    )
    print(f"training n_estimators={args.n_estimators} max_depth={max_depth}...")
    clf.fit(train_x, train_y)

    train_acc = accuracy_score(train_y, clf.predict(train_x))
    val_preds = clf.predict(val_x)
    val_acc = accuracy_score(val_y, val_preds)

    per_class_acc = {}
    for action_id in np.unique(val_y):
        mask = val_y == action_id
        per_class_acc[int(action_id)] = float(accuracy_score(val_y[mask], val_preds[mask]))

    macro_acc = float(np.mean(list(per_class_acc.values())))
    print(f"train_acc={train_acc:.4f} val_acc={val_acc:.4f} val_macro_acc={macro_acc:.4f}")

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    run_dir = output_dir / f"rf_baseline_{timestamp}"
    run_dir.mkdir(parents=True, exist_ok=True)
    model_path = run_dir / "rf_model.pkl"
    metadata_path = run_dir / "metadata.json"

    import pickle
    with model_path.open("wb") as f:
        pickle.dump(clf, f)

    action_dist = Counter(train_y.tolist())
    metadata = {
        "model_type": "random_forest",
        "input_dim": OBSERVATION_DIM,
        "output_dim": len(ACTION_ID_TO_NAME),
        "n_estimators": args.n_estimators,
        "max_depth": args.max_depth,
        "seed": args.seed,
        "train_samples": int(len(train_x)),
        "val_samples": int(len(val_x)),
        "val_accuracy": float(val_acc),
        "train_accuracy": float(train_acc),
        "val_macro_accuracy": float(macro_acc),
        "action_distribution": {ACTION_ID_TO_NAME[k]: int(v) for k, v in sorted(action_dist.items())},
        "val_per_class_accuracy": {ACTION_ID_TO_NAME[k]: v for k, v in sorted(per_class_acc.items())},
    }
    with metadata_path.open("w", encoding="utf-8") as f:
        json.dump(metadata, f, indent=2)

    print(f"saved_model={model_path}")
    print(f"saved_metadata={metadata_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())