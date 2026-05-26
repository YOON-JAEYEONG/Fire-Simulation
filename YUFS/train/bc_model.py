from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import torch
from torch import nn


@dataclass
class BCModelConfig:
    input_dim: int
    hidden_dims: tuple[int, ...]
    output_dim: int
    learning_rate: float = 1e-3
    weight_decay: float = 1e-5
    seed: int = 42


class BCMLPClassifier(nn.Module):
    def __init__(self, config: BCModelConfig):
        super().__init__()
        self.config = config
        torch.manual_seed(config.seed)

        layers: list[nn.Module] = []
        current_dim = config.input_dim

        for hidden_dim in config.hidden_dims:
            layers.append(nn.Linear(current_dim, hidden_dim))
            layers.append(nn.ReLU())
            current_dim = hidden_dim

        layers.append(nn.Linear(current_dim, config.output_dim))
        self.network = nn.Sequential(*layers)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.network(x)

    def predict(self, x: torch.Tensor) -> torch.Tensor:
        return torch.argmax(self.forward(x), dim=1)

    def save(self, output_path: str | Path, input_mean, input_std):
        path = Path(output_path)
        checkpoint = {
            "model_type": "behavior_cloning_mlp_torch",
            "config": {
                "input_dim": self.config.input_dim,
                "hidden_dims": list(self.config.hidden_dims),
                "output_dim": self.config.output_dim,
                "learning_rate": self.config.learning_rate,
                "weight_decay": self.config.weight_decay,
                "seed": self.config.seed,
            },
            "state_dict": self.state_dict(),
            "input_mean": torch.as_tensor(input_mean, dtype=torch.float32),
            "input_std": torch.as_tensor(input_std, dtype=torch.float32),
        }
        torch.save(checkpoint, path)
