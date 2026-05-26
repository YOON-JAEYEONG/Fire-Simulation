from __future__ import annotations

import argparse
from pathlib import Path

import torch
from torch import nn

try:
    from .bc_model import BCMLPClassifier, BCModelConfig
except ImportError:
    from bc_model import BCMLPClassifier, BCModelConfig


class NormalizedPolicyWrapper(nn.Module):
    def __init__(self, model: BCMLPClassifier, input_mean: torch.Tensor, input_std: torch.Tensor):
        super().__init__()
        self.model = model.eval()
        self.register_buffer("input_mean", input_mean.view(1, -1))
        self.register_buffer("input_std", input_std.view(1, -1))

    def forward(self, observation: torch.Tensor) -> torch.Tensor:
        normalized = (observation - self.input_mean) / self.input_std
        return self.model(normalized)


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Export a trained YUFS BC checkpoint to ONNX for Unreal NNE/ORT inference.")
    parser.add_argument("--checkpoint", required=True, help="Path to bc_model.pt produced by train_bc.py.")
    parser.add_argument("--output", required=True, help="Path to the exported ONNX file.")
    parser.add_argument("--opset", type=int, default=17, help="ONNX opset version.")
    return parser


def load_checkpoint(checkpoint_path: Path) -> tuple[BCMLPClassifier, torch.Tensor, torch.Tensor]:
    checkpoint = torch.load(checkpoint_path, map_location="cpu")
    config_dict = checkpoint["config"]

    model = BCMLPClassifier(
        BCModelConfig(
            input_dim=int(config_dict["input_dim"]),
            hidden_dims=tuple(int(value) for value in config_dict["hidden_dims"]),
            output_dim=int(config_dict["output_dim"]),
            learning_rate=float(config_dict.get("learning_rate", 1e-3)),
            weight_decay=float(config_dict.get("weight_decay", 1e-5)),
            seed=int(config_dict.get("seed", 42)),
        )
    )
    model.load_state_dict(checkpoint["state_dict"])
    model.eval()

    input_mean = torch.as_tensor(checkpoint["input_mean"], dtype=torch.float32)
    input_std = torch.as_tensor(checkpoint["input_std"], dtype=torch.float32)
    input_std = torch.where(input_std.abs() < 1e-6, torch.ones_like(input_std), input_std)

    return model, input_mean, input_std


def main() -> int:
    args = build_argument_parser().parse_args()
    checkpoint_path = Path(args.checkpoint)
    output_path = Path(args.output)

    if not checkpoint_path.is_file():
        raise SystemExit(f"Checkpoint not found: {checkpoint_path}")

    model, input_mean, input_std = load_checkpoint(checkpoint_path)
    wrapped_model = NormalizedPolicyWrapper(model, input_mean, input_std)

    dummy_input = torch.zeros((1, model.config.input_dim), dtype=torch.float32)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    try:
        torch.onnx.export(
            wrapped_model,
            dummy_input,
            output_path,
            export_params=True,
            opset_version=args.opset,
            do_constant_folding=True,
            input_names=["observation"],
            output_names=["action_logits"],
            dynamic_axes={
                "observation": {0: "batch"},
                "action_logits": {0: "batch"},
            },
            dynamo=False,
        )
    except ModuleNotFoundError as exc:
        raise SystemExit(
            "ONNX export failed because a required Python package is missing. "
            "Install the 'onnx' package in the training environment and try again."
        ) from exc

    print(f"saved_onnx={output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
