#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def load_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def metric_map(summary_rows: list[dict]) -> dict[str, dict]:
    return {row["metric"]: row for row in summary_rows}


def find_latest_artifact(output_root: Path) -> Path:
    candidates = sorted([p for p in output_root.iterdir() if p.is_dir()])
    if not candidates:
        raise FileNotFoundError(f"no perf artifacts found in {output_root}")
    return candidates[-1]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate CI-friendly baseline JSON from perf_scenarios output"
    )
    parser.add_argument(
        "artifact_dir", nargs="?", help="artifacts/perf/<timestamp> directory"
    )
    parser.add_argument(
        "--output",
        default="config/perf_baseline_default_profile.json",
        help="output JSON path",
    )
    parser.add_argument(
        "--output-root",
        default="artifacts/perf",
        help="perf artifact root when artifact_dir is omitted",
    )
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    artifact_dir = (
        Path(args.artifact_dir)
        if args.artifact_dir
        else find_latest_artifact(repo_root / args.output_root)
    )
    if not artifact_dir.is_absolute():
        artifact_dir = repo_root / artifact_dir

    summary = load_json(artifact_dir / "summary.json")
    extras = load_json(artifact_dir / "run_extras.json")
    metrics = metric_map(summary)

    atomic_ratios = []
    tiny_ratios = []
    for item in extras:
        values = item.get("extras", {})
        ratio = values.get("atomic_serial_time_ratio")
        if isinstance(ratio, (int, float)):
            atomic_ratios.append(float(ratio))
        tiny = values.get("tiny_batched_ratio")
        if isinstance(tiny, (int, float)):
            tiny_ratios.append(float(tiny))

    def require_metric(name: str) -> dict:
        row = metrics.get(name)
        if row is None:
            raise KeyError(f"missing metric in summary.json: {name}")
        return row

    output = {
        "version": 1,
        "profile": {
            "width": 400,
            "height": 200,
            "initial_colonies": 50,
            "build_type": "Release",
            "scale": 2,
            "repeats": 3,
        },
        "artifact_dir": str(artifact_dir.relative_to(repo_root)),
        "metrics": {
            "simulation_tick_serial_ms": require_metric("simulation_tick (serial)")[
                "median_ms"
            ],
            "atomic_tick_1_thread_ms": require_metric("atomic_tick (1 thread)")[
                "median_ms"
            ],
            "atomic_tick_2_threads_ms": require_metric("atomic_tick (2 threads)")[
                "median_ms"
            ],
            "atomic_tick_4_threads_ms": require_metric("atomic_tick (4 threads)")[
                "median_ms"
            ],
            "broadcast_build_snapshot_ms": require_metric("broadcast build snapshot")[
                "median_ms"
            ],
            "broadcast_build_serialize_ms": require_metric("broadcast build+serialize")[
                "median_ms"
            ],
            "broadcast_end_to_end_ms": require_metric(
                "broadcast end-to-end (0 clients)"
            )["median_ms"],
            "threadpool_tiny_tasks_ms": require_metric("threadpool tiny tasks")[
                "median_ms"
            ],
            "threadpool_chunked_submit_ms": require_metric("threadpool chunked submit")[
                "median_ms"
            ],
            "threadpool_batched_tasks_ms": require_metric("threadpool batched tasks")[
                "median_ms"
            ],
            "server_snapshot_build_ms": require_metric("server snapshot build")[
                "median_ms"
            ],
            "protocol_serialize_deserialize_ms": require_metric(
                "protocol serialize+deserialize"
            )["median_ms"],
        },
        "ratios": {
            "atomic_serial_time_ratio": round(
                sum(atomic_ratios) / len(atomic_ratios), 4
            )
            if atomic_ratios
            else None,
            "tiny_batched_ratio": round(sum(tiny_ratios) / len(tiny_ratios), 4)
            if tiny_ratios
            else None,
        },
    }

    out_path = Path(args.output)
    if not out_path.is_absolute():
        out_path = repo_root / out_path
    out_path.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    print(out_path.relative_to(repo_root))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
