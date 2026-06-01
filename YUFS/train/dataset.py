from __future__ import annotations

import csv
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Iterator

try:
    from .schema import ACTION_NAME_TO_ID, OBSERVATION_DIM, TERMINAL_NAME_TO_ID
except ImportError:
    from schema import ACTION_NAME_TO_ID, OBSERVATION_DIM, TERMINAL_NAME_TO_ID


@dataclass(frozen=True)
class Transition:
    run_id: int
    agent_id: str
    step_index: int
    sim_frame: int
    sim_time_seconds: float
    action_name: str
    action_id: int
    done: bool
    terminal_reason: str
    terminal_reason_id: int
    state: list[float]
    next_state: list[float]


def parse_observation_vector(raw_value: str) -> list[float]:
    values = [float(part) for part in raw_value.split(";") if part]
    if len(values) != OBSERVATION_DIM:
        raise ValueError(
            f"Expected observation dimension {OBSERVATION_DIM}, got {len(values)} from value: {raw_value!r}"
        )
    return values


def parse_transition_row(row: dict[str, str]) -> Transition:
    action_name = row["action"]
    terminal_reason = row["terminal_reason"]

    if action_name not in ACTION_NAME_TO_ID:
        raise KeyError(f"Unknown action name {action_name!r}")
    if terminal_reason not in TERMINAL_NAME_TO_ID:
        raise KeyError(f"Unknown terminal reason {terminal_reason!r}")

    return Transition(
        run_id=int(row["run_id"]),
        agent_id=row["agent_id"],
        step_index=int(row["step_index"]),
        sim_frame=int(row["sim_frame"]),
        sim_time_seconds=float(row["sim_time_seconds"]),
        action_name=action_name,
        action_id=ACTION_NAME_TO_ID[action_name],
        done=row["done"] == "1",
        terminal_reason=terminal_reason,
        terminal_reason_id=TERMINAL_NAME_TO_ID[terminal_reason],
        state=parse_observation_vector(row["state"]),
        next_state=parse_observation_vector(row["next_state"]),
    )


def iter_transition_rows(csv_path: Path) -> Iterator[dict[str, str]]:
    with csv_path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            yield row


def load_transitions(csv_path: str | Path) -> list[Transition]:
    path = Path(csv_path)
    return [parse_transition_row(row) for row in iter_transition_rows(path)]


def iter_transitions(csv_path: str | Path) -> Iterator[Transition]:
    path = Path(csv_path)
    for row in iter_transition_rows(path):
        yield parse_transition_row(row)


REQUIRED_COLUMNS = {"run_id", "agent_id", "step_index", "sim_frame", "sim_time_seconds",
                    "action", "done", "terminal_reason", "state", "next_state"}


def _has_required_columns(csv_path: Path) -> bool:
    try:
        with csv_path.open("r", encoding="utf-8", newline="") as handle:
            header_line = handle.readline()
        columns = {col.strip().lstrip("﻿") for col in header_line.split(",")}
        return REQUIRED_COLUMNS.issubset(columns)
    except OSError:
        return False


def find_transition_logs(root: str | Path) -> list[Path]:
    base_path = Path(root)
    if base_path.is_file():
        return [base_path]

    return sorted(base_path.glob("*.csv"))


def load_transition_directory(root: str | Path) -> list[Transition]:
    transitions: list[Transition] = []
    for csv_path in find_transition_logs(root):
        if not _has_required_columns(csv_path):
            print(f"skipped_incompatible_file={csv_path.name}")
            continue
        transitions.extend(load_transitions(csv_path))
    return transitions


def transitions_to_numpy_dict(transitions: Iterable[Transition]):
    import numpy as np

    transition_list = list(transitions)
    return {
        "state": np.asarray([item.state for item in transition_list], dtype=np.float32),
        "action": np.asarray([item.action_id for item in transition_list], dtype=np.int64),
        "next_state": np.asarray([item.next_state for item in transition_list], dtype=np.float32),
        "done": np.asarray([item.done for item in transition_list], dtype=np.bool_),
        "run_id": np.asarray([item.run_id for item in transition_list], dtype=np.int64),
        "step_index": np.asarray([item.step_index for item in transition_list], dtype=np.int64),
        "terminal_reason": np.asarray([item.terminal_reason_id for item in transition_list], dtype=np.int64),
    }
