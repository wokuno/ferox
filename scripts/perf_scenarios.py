#!/usr/bin/env python3
"""Run Ferox performance tests across scenarios and summarize bottlenecks.

Usage:
  uv run scripts/perf_scenarios.py
  uv run scripts/perf_scenarios.py --build-types Debug Release --scales 1 2 --repeats 2
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import re
import statistics
import subprocess
import sys
from dataclasses import dataclass, asdict
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Tuple


METRIC_RE = re.compile(
    r"\[perf\]\s+(.+?)\s+([0-9]+(?:\.[0-9]+)?)\s+ms,\s+([0-9]+(?:\.[0-9]+)?)\s+ops/s"
)
PROTOCOL_MBPS_RE = re.compile(
    r"\[perf\]\s+protocol throughput:\s+([0-9]+(?:\.[0-9]+)?)\s+MB/s"
)
ATOMIC_SERIAL_RATIO_RE = re.compile(
    r"\[perf\]\s+atomic/serial time ratio:\s+([0-9]+(?:\.[0-9]+)?)x"
)
SPEEDUP_RE = re.compile(
    r"\[perf\]\s+speedup vs 1-thread:\s+2-thread=([0-9]+(?:\.[0-9]+)?)x\s+4-thread=([0-9]+(?:\.[0-9]+)?)x"
)
TINY_BATCHED_RATIO_RE = re.compile(
    r"\[perf\]\s+tiny/batched ratio:\s+([0-9]+(?:\.[0-9]+)?)x"
)
HINT_RE = re.compile(r"\[hint\]\s+(.+)")


@dataclass
class MetricPoint:
    scenario: str
    build_type: str
    scale: int
    repeat: int
    metric: str
    ms: float
    ops_per_sec: float


def run_cmd(
    cmd: List[str], cwd: Path, env: Dict[str, str] | None = None
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd,
        cwd=str(cwd),
        env=env,
        text=True,
        capture_output=True,
        check=False,
    )


def configure_build(project_root: Path, build_dir: Path, build_type: str) -> None:
    build_dir.mkdir(parents=True, exist_ok=True)
    cfg = run_cmd(
        [
            "cmake",
            "-S",
            str(project_root),
            "-B",
            str(build_dir),
            f"-DCMAKE_BUILD_TYPE={build_type}",
        ],
        cwd=project_root,
    )
    if cfg.returncode != 0:
        print(cfg.stdout)
        print(cfg.stderr, file=sys.stderr)
        raise RuntimeError(f"CMake configure failed for {build_type}")

    build = run_cmd(
        ["cmake", "--build", str(build_dir), "--parallel"], cwd=project_root
    )
    if build.returncode != 0:
        print(build.stdout)
        print(build.stderr, file=sys.stderr)
        raise RuntimeError(f"Build failed for {build_type}")


def run_perf_ctest(
    build_dir: Path, scale: int, test_regex: str
) -> Tuple[int, str, str]:
    env = dict(**os.environ)
    env["FEROX_PERF_SCALE"] = str(scale)
    proc = run_cmd(
        ["ctest", "--output-on-failure", "-R", test_regex, "-V"],
        cwd=build_dir,
        env=env,
    )
    return proc.returncode, proc.stdout, proc.stderr


def parse_metrics(
    output: str, scenario: str, build_type: str, scale: int, repeat: int
) -> Tuple[List[MetricPoint], Dict[str, float], List[str]]:
    points: List[MetricPoint] = []
    extras: Dict[str, float] = {}
    hints: List[str] = []

    for m in METRIC_RE.finditer(output):
        points.append(
            MetricPoint(
                scenario=scenario,
                build_type=build_type,
                scale=scale,
                repeat=repeat,
                metric=m.group(1).strip(),
                ms=float(m.group(2)),
                ops_per_sec=float(m.group(3)),
            )
        )

    mbps = PROTOCOL_MBPS_RE.search(output)
    if mbps:
        extras["protocol_throughput_mb_s"] = float(mbps.group(1))

    ratio = ATOMIC_SERIAL_RATIO_RE.search(output)
    if ratio:
        extras["atomic_serial_time_ratio"] = float(ratio.group(1))

    speed = SPEEDUP_RE.search(output)
    if speed:
        extras["speedup_2thread"] = float(speed.group(1))
        extras["speedup_4thread"] = float(speed.group(2))

    tiny = TINY_BATCHED_RATIO_RE.search(output)
    if tiny:
        extras["tiny_batched_ratio"] = float(tiny.group(1))

    hints.extend([h.group(1).strip() for h in HINT_RE.finditer(output)])
    return points, extras, hints


def summarize(points: List[MetricPoint]) -> List[dict]:
    buckets: Dict[Tuple[str, str], List[MetricPoint]] = {}
    for p in points:
        buckets.setdefault((p.scenario, p.metric), []).append(p)

    rows: List[dict] = []
    for (scenario, metric), vals in sorted(buckets.items()):
        ms_values = [v.ms for v in vals]
        ops_values = [v.ops_per_sec for v in vals]
        rows.append(
            {
                "scenario": scenario,
                "metric": metric,
                "samples": len(vals),
                "median_ms": round(statistics.median(ms_values), 4),
                "mean_ms": round(statistics.fmean(ms_values), 4),
                "median_ops_per_sec": round(statistics.median(ops_values), 4),
            }
        )
    return rows


def detect_bottlenecks(
    summary_rows: List[dict], run_extras: List[dict], run_hints: List[dict]
) -> List[str]:
    notes: List[str] = []

    if summary_rows:
        slowest = sorted(summary_rows, key=lambda r: r["median_ms"], reverse=True)[:5]
        notes.append("Slowest median metrics:")
        for row in slowest:
            notes.append(
                f"- {row['scenario']} :: {row['metric']} = {row['median_ms']:.2f} ms"
            )

    ratios = [
        e["extras"].get("atomic_serial_time_ratio")
        for e in run_extras
        if e["extras"].get("atomic_serial_time_ratio") is not None
    ]
    ratios = [r for r in ratios if r is not None]
    if ratios and statistics.fmean(ratios) > 1.20:
        notes.append(
            f"Atomic path is slower than serial on average (atomic/serial ratio {statistics.fmean(ratios):.2f}x)."
        )

    speedup4 = [
        e["extras"].get("speedup_4thread")
        for e in run_extras
        if e["extras"].get("speedup_4thread") is not None
    ]
    speedup4 = [s for s in speedup4 if s is not None]
    if speedup4 and statistics.fmean(speedup4) < 1.10:
        notes.append(
            f"4-thread scaling is weak (avg speedup {statistics.fmean(speedup4):.2f}x)."
        )

    tiny_ratio = [
        e["extras"].get("tiny_batched_ratio")
        for e in run_extras
        if e["extras"].get("tiny_batched_ratio") is not None
    ]
    tiny_ratio = [t for t in tiny_ratio if t is not None]
    if tiny_ratio and statistics.fmean(tiny_ratio) > 2.0:
        notes.append(
            f"Threadpool tiny-task overhead is high (tiny/batched ratio {statistics.fmean(tiny_ratio):.2f}x)."
        )

    unique_hints = sorted({h for item in run_hints for h in item.get("hints", [])})
    if unique_hints:
        notes.append("Perf-test hints observed:")
        for h in unique_hints:
            notes.append(f"- {h}")

    return notes


def write_outputs(
    output_dir: Path,
    points: List[MetricPoint],
    run_extras: List[dict],
    run_hints: List[dict],
    summary_rows: List[dict],
    bottlenecks: List[str],
) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)

    (output_dir / "raw_metrics.json").write_text(
        json.dumps([asdict(p) for p in points], indent=2),
        encoding="utf-8",
    )
    (output_dir / "run_extras.json").write_text(
        json.dumps(run_extras, indent=2), encoding="utf-8"
    )
    (output_dir / "run_hints.json").write_text(
        json.dumps(run_hints, indent=2), encoding="utf-8"
    )
    (output_dir / "summary.json").write_text(
        json.dumps(summary_rows, indent=2), encoding="utf-8"
    )

    with (output_dir / "metrics.csv").open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(
            ["scenario", "build_type", "scale", "repeat", "metric", "ms", "ops_per_sec"]
        )
        for p in points:
            w.writerow(
                [
                    p.scenario,
                    p.build_type,
                    p.scale,
                    p.repeat,
                    p.metric,
                    f"{p.ms:.6f}",
                    f"{p.ops_per_sec:.6f}",
                ]
            )

    md = ["# Performance Scenario Report", "", "## Bottlenecks"]
    if bottlenecks:
        md.extend(bottlenecks)
    else:
        md.append("- No clear bottlenecks detected from collected scenarios.")

    md.append("")
    md.append("## Scenario Summary")
    for row in sorted(summary_rows, key=lambda r: (r["scenario"], -r["median_ms"])):
        md.append(
            f"- {row['scenario']} | {row['metric']} | median_ms={row['median_ms']:.2f} | median_ops={row['median_ops_per_sec']:.2f}"
        )

    (output_dir / "report.md").write_text("\n".join(md) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run Ferox performance tests across scenarios."
    )
    parser.add_argument("--build-types", nargs="+", default=["Debug", "Release"])
    parser.add_argument("--scales", nargs="+", type=int, default=[1, 2])
    parser.add_argument("--repeats", type=int, default=2)
    parser.add_argument("--test-regex", default="PerformanceEvalTests|SimdEvalTests")
    parser.add_argument("--build-root", default="build-perf")
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--output-root", default="artifacts/perf")
    args = parser.parse_args()

    project_root = Path(__file__).resolve().parents[1]
    build_root = project_root / args.build_root
    out_dir = project_root / args.output_root / datetime.now().strftime("%Y%m%d-%H%M%S")

    points: List[MetricPoint] = []
    run_extras: List[dict] = []
    run_hints: List[dict] = []

    for build_type in args.build_types:
        build_dir = build_root / build_type.lower()
        print(f"\n=== Build: {build_type} ({build_dir}) ===")
        if not args.skip_build:
            configure_build(project_root, build_dir, build_type)

        for scale in args.scales:
            for rep in range(1, args.repeats + 1):
                scenario = f"{build_type.lower()}_scale{scale}"
                print(f"\n--- Scenario {scenario} run {rep}/{args.repeats} ---")
                rc, stdout, stderr = run_perf_ctest(build_dir, scale, args.test_regex)
                if stderr.strip():
                    print(stderr.strip(), file=sys.stderr)

                run_log = out_dir / "logs" / f"{scenario}_run{rep}.log"
                run_log.parent.mkdir(parents=True, exist_ok=True)
                run_log.write_text(stdout, encoding="utf-8")

                p, extras, hints = parse_metrics(
                    stdout, scenario, build_type, scale, rep
                )
                points.extend(p)
                run_extras.append(
                    {
                        "scenario": scenario,
                        "build_type": build_type,
                        "scale": scale,
                        "repeat": rep,
                        "returncode": rc,
                        "extras": extras,
                    }
                )
                run_hints.append(
                    {
                        "scenario": scenario,
                        "build_type": build_type,
                        "scale": scale,
                        "repeat": rep,
                        "hints": hints,
                    }
                )

                if rc != 0:
                    print(
                        f"WARNING: ctest returned {rc} for {scenario} run {rep}",
                        file=sys.stderr,
                    )

    summary_rows = summarize(points)
    bottlenecks = detect_bottlenecks(summary_rows, run_extras, run_hints)
    write_outputs(out_dir, points, run_extras, run_hints, summary_rows, bottlenecks)

    print("\n=== Done ===")
    print(f"Artifacts: {out_dir}")
    print(f"Metric samples: {len(points)}")
    for line in bottlenecks:
        print(line)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
