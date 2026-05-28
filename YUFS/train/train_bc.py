from __future__ import annotations

import argparse
import json
from collections import Counter
from datetime import datetime
from pathlib import Path

import numpy as np
import torch
from torch import nn

try:
    from .bc_model import BCMLPClassifier, BCModelConfig
    from .dataset import load_transition_directory
    from .schema import ACTION_ID_TO_NAME, OBSERVATION_DIM, OBSERVATION_FIELDS
except ImportError:
    from bc_model import BCMLPClassifier, BCModelConfig
    from dataset import load_transition_directory
    from schema import ACTION_ID_TO_NAME, OBSERVATION_DIM, OBSERVATION_FIELDS


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Train a behavior cloning baseline from YUFS transition logs.")
    parser.add_argument("--data", default=str(Path("Saved") / "RLTransitions"), help="CSV file or directory of transition logs.")
    parser.add_argument(
        "--output-dir",
        default=str(Path("Saved") / "RLModels"),
        help="Directory where the trained model and metadata will be saved.",
    )
    parser.add_argument("--epochs", type=int, default=30, help="Number of training epochs.")
    parser.add_argument("--batch-size", type=int, default=1024, help="Mini-batch size.")
    parser.add_argument("--learning-rate", type=float, default=1e-3, help="Learning rate.")
    parser.add_argument("--weight-decay", type=float, default=1e-5, help="L2 regularization strength.")
    parser.add_argument("--hidden-dims", default="128,128", help="Comma-separated hidden layer sizes.")
    parser.add_argument("--val-ratio", type=float, default=0.2, help="Validation split ratio per action class.")
    parser.add_argument("--seed", type=int, default=42, help="Random seed.")
    parser.add_argument("--max-samples", type=int, default=0, help="Optional cap on total transitions used for training.")
    parser.add_argument(
        "--class-weight-power",
        type=float,
        default=0.5,
        help="Power used for inverse-frequency class weights. 0 disables weighting, 1 is full inverse frequency.",
    )
    parser.add_argument(
        "--device",
        choices=("auto", "cpu", "cuda"),
        default="auto",
        help="Training device. 'auto' prefers CUDA when available.",
    )
    return parser


def parse_hidden_dims(raw_value: str) -> tuple[int, ...]:
    parts = [item.strip() for item in raw_value.split(",") if item.strip()]
    if not parts:
        return ()
    return tuple(int(item) for item in parts)


def choose_device(device_arg: str) -> torch.device:
    if device_arg == "cpu":
        return torch.device("cpu")
    if device_arg == "cuda":
        if not torch.cuda.is_available():
            raise SystemExit("CUDA was requested, but torch.cuda.is_available() is False.")
        return torch.device("cuda")
    return torch.device("cuda" if torch.cuda.is_available() else "cpu")


def set_random_seed(seed: int):
    np.random.seed(seed)
    torch.manual_seed(seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)


def stratified_split_indices(labels: np.ndarray, val_ratio: float, seed: int):
    rng = np.random.default_rng(seed)
    train_indices: list[np.ndarray] = []
    val_indices: list[np.ndarray] = []

    for action_id in np.unique(labels):
        class_indices = np.flatnonzero(labels == action_id)
        shuffled = class_indices.copy()
        rng.shuffle(shuffled)

        val_count = int(round(shuffled.size * val_ratio))
        val_count = max(1, val_count) if shuffled.size > 1 and val_ratio > 0.0 else min(val_count, shuffled.size)
        val_count = min(val_count, max(0, shuffled.size - 1)) if shuffled.size > 1 else 0

        val_indices.append(shuffled[:val_count])
        train_indices.append(shuffled[val_count:])

    train_index = np.concatenate(train_indices) if train_indices else np.empty((0,), dtype=np.int64)
    val_index = np.concatenate(val_indices) if val_indices else np.empty((0,), dtype=np.int64)
    return train_index, val_index


def normalize_features(train_x: np.ndarray, val_x: np.ndarray):
    input_mean = train_x.mean(axis=0)
    input_std = train_x.std(axis=0)
    input_std = np.where(input_std < 1e-6, 1.0, input_std)
    return (train_x - input_mean) / input_std, (val_x - input_mean) / input_std, input_mean, input_std


def compute_class_weights(labels: np.ndarray, num_classes: int, power: float):
    counts = np.bincount(labels, minlength=num_classes).astype(np.float32)
    weights = np.ones((num_classes,), dtype=np.float32)
    non_zero = counts > 0

    if power <= 0.0:
        return weights

    max_count = float(np.max(counts[non_zero]))
    weights[non_zero] = (max_count / counts[non_zero]) ** power
    return weights


def make_output_paths(output_dir: Path):
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    run_dir = output_dir / f"bc_baseline_{timestamp}"
    run_dir.mkdir(parents=True, exist_ok=True)
    return run_dir, run_dir / "bc_model.pt", run_dir / "metadata.json"


def random_subsample(transitions, max_samples: int, seed: int):
    if max_samples <= 0 or len(transitions) <= max_samples:
        return transitions

    rng = np.random.default_rng(seed)
    indices = rng.choice(len(transitions), size=max_samples, replace=False)
    indices.sort()
    return [transitions[index] for index in indices]


def batch_indices(num_samples: int, batch_size: int, rng: np.random.Generator):
    permutation = rng.permutation(num_samples)
    for start in range(0, num_samples, batch_size):
        yield permutation[start : start + batch_size]


def evaluate(model: BCMLPClassifier, x: torch.Tensor, y: torch.Tensor, batch_size: int):
    model.eval()
    predictions: list[torch.Tensor] = []

    with torch.no_grad():
        for start in range(0, x.shape[0], batch_size):
            logits = model(x[start : start + batch_size])
            predictions.append(torch.argmax(logits, dim=1).cpu())

    all_predictions = torch.cat(predictions)
    y_cpu = y.cpu()
    accuracy = float((all_predictions == y_cpu).float().mean().item())

    per_class_accuracy: dict[int, float] = {}
    for action_id in torch.unique(y_cpu):
        class_mask = y_cpu == action_id
        class_accuracy = (all_predictions[class_mask] == y_cpu[class_mask]).float().mean().item()
        per_class_accuracy[int(action_id.item())] = float(class_accuracy)

    macro_accuracy = float(np.mean(list(per_class_accuracy.values()))) if per_class_accuracy else 0.0
    return accuracy, macro_accuracy, per_class_accuracy


def main() -> int:
    args = build_argument_parser().parse_args()
    output_dir = Path(args.output_dir)
    hidden_dims = parse_hidden_dims(args.hidden_dims)
    device = choose_device(args.device)

    set_random_seed(args.seed)

    transitions = load_transition_directory(args.data)
    if not transitions:
        raise SystemExit(f"No transitions found under {args.data}")

    transitions = random_subsample(transitions, args.max_samples, args.seed)

    states = np.asarray([item.state for item in transitions], dtype=np.float32)
    actions = np.asarray([item.action_id for item in transitions], dtype=np.int64)
    num_classes = len(ACTION_ID_TO_NAME)
    unique_actions = np.unique(actions)

    print(f"loaded_transitions={len(transitions)}")
    print("action_coverage=" + ",".join(ACTION_ID_TO_NAME[int(action_id)] for action_id in unique_actions))
    print(f"device={device.type}")
    if device.type == "cuda":
        print(f"cuda_device={torch.cuda.get_device_name(0)}")
    if unique_actions.size == 1:
        print("warning=dataset currently contains only one action class; this baseline will reduce to a trivial imitator")

    train_index, val_index = stratified_split_indices(actions, args.val_ratio, args.seed)
    if train_index.size == 0:
        raise SystemExit("Training split is empty. Adjust the validation ratio or collect more data.")

    train_x = states[train_index]
    train_y = actions[train_index]
    val_x = states[val_index] if val_index.size > 0 else states[train_index]
    val_y = actions[val_index] if val_index.size > 0 else actions[train_index]

    train_x, val_x, input_mean, input_std = normalize_features(train_x, val_x)
    class_weights = compute_class_weights(train_y, num_classes, args.class_weight_power)

    train_x_tensor = torch.from_numpy(train_x).to(device=device, dtype=torch.float32)
    train_y_tensor = torch.from_numpy(train_y).to(device=device, dtype=torch.long)
    val_x_tensor = torch.from_numpy(val_x).to(device=device, dtype=torch.float32)
    val_y_tensor = torch.from_numpy(val_y).to(device=device, dtype=torch.long)
    class_weights_tensor = torch.from_numpy(class_weights).to(device=device, dtype=torch.float32)

    model = BCMLPClassifier(
        BCModelConfig(
            input_dim=OBSERVATION_DIM,
            hidden_dims=hidden_dims,
            output_dim=num_classes,
            learning_rate=args.learning_rate,
            weight_decay=args.weight_decay,
            seed=args.seed,
        )
    ).to(device)

    optimizer = torch.optim.AdamW(model.parameters(), lr=args.learning_rate, weight_decay=args.weight_decay)
    loss_fn = nn.CrossEntropyLoss(weight=class_weights_tensor)
    rng = np.random.default_rng(args.seed)

    best_val_accuracy = -1.0
    best_state_dict = None
    best_per_class: dict[int, float] = {}
    history: list[dict[str, float]] = []

    for epoch in range(1, args.epochs + 1):
        model.train()
        batch_losses: list[float] = []
        batch_accuracies: list[float] = []

        for index_batch in batch_indices(train_x_tensor.shape[0], args.batch_size, rng):
            batch_x = train_x_tensor[index_batch]
            batch_y = train_y_tensor[index_batch]

            optimizer.zero_grad(set_to_none=True)
            logits = model(batch_x)
            loss = loss_fn(logits, batch_y)
            loss.backward()
            optimizer.step()

            predictions = torch.argmax(logits, dim=1)
            accuracy = (predictions == batch_y).float().mean().item()
            batch_losses.append(float(loss.item()))
            batch_accuracies.append(float(accuracy))

        train_accuracy, train_macro_accuracy, _ = evaluate(model, train_x_tensor, train_y_tensor, args.batch_size)
        val_accuracy, val_macro_accuracy, val_per_class = evaluate(model, val_x_tensor, val_y_tensor, args.batch_size)

        epoch_record = {
            "epoch": float(epoch),
            "loss": float(np.mean(batch_losses)),
            "batch_accuracy": float(np.mean(batch_accuracies)),
            "train_accuracy": train_accuracy,
            "train_macro_accuracy": train_macro_accuracy,
            "val_accuracy": val_accuracy,
            "val_macro_accuracy": val_macro_accuracy,
        }
        history.append(epoch_record)

        print(
            f"epoch={epoch:03d} loss={epoch_record['loss']:.6f} "
            f"train_acc={train_accuracy:.4f} val_acc={val_accuracy:.4f} "
            f"val_macro_acc={val_macro_accuracy:.4f}"
        )

        if val_accuracy > best_val_accuracy:
            best_val_accuracy = val_accuracy
            best_state_dict = {key: value.detach().cpu().clone() for key, value in model.state_dict().items()}
            best_per_class = dict(val_per_class)

    if best_state_dict is not None:
        model.load_state_dict(best_state_dict)

    run_dir, model_path, metadata_path = make_output_paths(output_dir)
    model.save(model_path, input_mean, input_std)

    action_distribution = Counter(train_y.tolist())
    metadata = {
        "model_type": "behavior_cloning_mlp_torch",
        "device": device.type,
        "input_dim": OBSERVATION_DIM,
        "hidden_dims": list(hidden_dims),
        "output_dim": num_classes,
        "epochs": args.epochs,
        "batch_size": args.batch_size,
        "learning_rate": args.learning_rate,
        "weight_decay": args.weight_decay,
        "seed": args.seed,
        "train_samples": int(train_x.shape[0]),
        "val_samples": int(val_x.shape[0]),
        "best_val_accuracy": float(best_val_accuracy),
        "best_val_macro_accuracy": float(max((item["val_macro_accuracy"] for item in history), default=0.0)),
        "class_weight_power": args.class_weight_power,
        "class_weights": class_weights.tolist(),
        "action_distribution": {ACTION_ID_TO_NAME[action_id]: int(count) for action_id, count in sorted(action_distribution.items())},
        "val_per_class_accuracy": {
            ACTION_ID_TO_NAME[action_id]: float(accuracy) for action_id, accuracy in sorted(best_per_class.items())
        },
        "observation_schema": [
            {"index": field.index, "name": field.name, "description": field.description}
            for field in OBSERVATION_FIELDS
        ],
        "history": history,
    }

    with metadata_path.open("w", encoding="utf-8") as handle:
        json.dump(metadata, handle, indent=2)

    print(f"saved_model={model_path}")
    print(f"saved_metadata={metadata_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
