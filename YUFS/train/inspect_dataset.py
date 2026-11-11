from __future__ import annotations

import argparse
from collections import Counter
from pathlib import Path

try:
    from .dataset import find_transition_logs, load_transition_directory
    from .schema import ACTION_ID_TO_NAME, OBSERVATION_FIELDS, TERMINAL_ID_TO_NAME
except ImportError:
    from dataset import find_transition_logs, load_transition_directory
    from schema import ACTION_ID_TO_NAME, OBSERVATION_FIELDS, TERMINAL_ID_TO_NAME


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Inspect YUFS RL transition CSV logs.")
    parser.add_argument(
        "path",
        nargs="?",
        default=Path("Saved") / "RLTransitions",
        help="Path to a transition CSV file or a directory containing CSV files.",
    )
    parser.add_argument(
        "--preview",
        type=int,
        default=3,
        help="How many transitions to preview from the beginning of the dataset.",
    )
    return parser


def main() -> int:
    args = build_argument_parser().parse_args()
    root = Path(args.path)
    csv_files = find_transition_logs(root)

    if not csv_files:
        print(f"No transition CSV files found at {root}")
        return 1

    transitions = load_transition_directory(root)
    if not transitions:
        print(f"No transitions loaded from {root}")
        return 1

    action_counts = Counter(item.action_id for item in transitions)
    terminal_counts = Counter(item.terminal_reason_id for item in transitions)
    done_count = sum(1 for item in transitions if item.done)
    run_ids = sorted({item.run_id for item in transitions})
    agent_ids = sorted({item.agent_id for item in transitions})

    print(f"log_files: {len(csv_files)}")
    print(f"transitions: {len(transitions)}")
    print(f"runs: {len(run_ids)} -> {run_ids}")
    print(f"agents: {len(agent_ids)}")
    print(f"done_transitions: {done_count}")

    print("\naction_counts:")
    for action_id, count in sorted(action_counts.items()):
        print(f"  {ACTION_ID_TO_NAME[action_id]}: {count}")

    print("\nterminal_reason_counts:")
    for terminal_id, count in sorted(terminal_counts.items()):
        print(f"  {TERMINAL_ID_TO_NAME[terminal_id]}: {count}")

    print("\nobservation_schema:")
    for field in OBSERVATION_FIELDS:
        print(f"  [{field.index:02d}] {field.name}")

    print("\npreview:")
    for item in transitions[: max(args.preview, 0)]:
        print(
            f"  run={item.run_id} agent={item.agent_id} step={item.step_index} "
            f"action={item.action_name} done={int(item.done)} "
            f"terminal={item.terminal_reason}"
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
