#!/usr/bin/env python3

import argparse
import json
import pathlib
import sys


CANONICAL_SCENARIOS = {
    "nutrient_limited_growth",
    "toxin_duel",
    "quorum_activation_wave",
    "persistence_stress_response",
    "frontier_genetic_drift",
    "coexistence_dynamics",
}


def load_config(path: pathlib.Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        raise ValueError(f"config file not found: {path}")
    except json.JSONDecodeError as exc:
        raise ValueError(f"invalid JSON at {path}:{exc.lineno}:{exc.colno}: {exc.msg}")


def validate_config(data: dict, strict: bool) -> list[str]:
    errors = []
    if not isinstance(data, dict):
        return ["top-level JSON object is required"]

    scenarios = data.get("scenarios")
    if not isinstance(scenarios, list) or not scenarios:
        return ["scenarios must be a non-empty array"]

    seen_ids = set()
    for index, scenario in enumerate(scenarios):
        prefix = f"scenarios[{index}]"
        if not isinstance(scenario, dict):
            errors.append(f"{prefix} must be an object")
            continue

        scenario_id = scenario.get("id")
        if not isinstance(scenario_id, str) or not scenario_id:
            errors.append(f"{prefix}.id must be a non-empty string")
        elif scenario_id in seen_ids:
            errors.append(f"duplicate scenario id: {scenario_id}")
        else:
            seen_ids.add(scenario_id)

        for key in ("title", "setup", "run", "metrics"):
            if key not in scenario:
                errors.append(f"{prefix}.{key} is required")

        setup = scenario.get("setup", {})
        run = scenario.get("run", {})
        metrics = scenario.get("metrics", [])

        world = setup.get("world") if isinstance(setup, dict) else None
        if not isinstance(world, dict):
            errors.append(f"{prefix}.setup.world must be an object")
        else:
            for dim in ("width", "height"):
                dim_value = world.get(dim)
                if not isinstance(dim_value, int) or dim_value <= 0:
                    errors.append(
                        f"{prefix}.setup.world.{dim} must be a positive integer"
                    )

        if isinstance(setup, dict):
            for key in ("initial_colonies", "seed"):
                value = setup.get(key)
                if not isinstance(value, int) or value <= 0:
                    errors.append(f"{prefix}.setup.{key} must be a positive integer")
        else:
            errors.append(f"{prefix}.setup must be an object")

        if isinstance(run, dict):
            for key in (
                "ticks",
                "replicates",
                "warmup_ticks",
                "tick_rate_ms",
                "threads",
            ):
                value = run.get(key)
                if not isinstance(value, int) or value <= 0:
                    errors.append(f"{prefix}.run.{key} must be a positive integer")
            ticks = run.get("ticks")
            warmup = run.get("warmup_ticks")
            if isinstance(ticks, int) and isinstance(warmup, int) and warmup >= ticks:
                errors.append(f"{prefix}.run.warmup_ticks must be less than ticks")
        else:
            errors.append(f"{prefix}.run must be an object")

        if not isinstance(metrics, list) or len(metrics) == 0:
            errors.append(f"{prefix}.metrics must be a non-empty array")
        else:
            metric_ids = set()
            for metric_index, metric in enumerate(metrics):
                mprefix = f"{prefix}.metrics[{metric_index}]"
                if not isinstance(metric, dict):
                    errors.append(f"{mprefix} must be an object")
                    continue
                metric_id = metric.get("id")
                if not isinstance(metric_id, str) or not metric_id:
                    errors.append(f"{mprefix}.id must be a non-empty string")
                elif metric_id in metric_ids:
                    errors.append(f"duplicate metric id in {scenario_id}: {metric_id}")
                else:
                    metric_ids.add(metric_id)

                pass_band = metric.get("pass_band")
                if not isinstance(pass_band, dict):
                    errors.append(f"{mprefix}.pass_band must be an object")
                    continue
                lo = pass_band.get("min")
                hi = pass_band.get("max")
                if not isinstance(lo, (int, float)) or not isinstance(hi, (int, float)):
                    errors.append(f"{mprefix}.pass_band min/max must be numeric")
                elif lo > hi:
                    errors.append(f"{mprefix}.pass_band min must be <= max")

    if strict:
        missing = sorted(CANONICAL_SCENARIOS - seen_ids)
        extra = sorted(seen_ids - CANONICAL_SCENARIOS)
        if missing:
            errors.append("missing canonical scenarios: " + ", ".join(missing))
        if extra:
            errors.append("unexpected scenarios in strict mode: " + ", ".join(extra))

    return errors


def print_run_plan(data: dict, scenario_filter: str) -> int:
    scenarios = data.get("scenarios", [])
    selected = scenarios
    if scenario_filter != "all":
        selected = [s for s in scenarios if s.get("id") == scenario_filter]
        if not selected:
            print(f"error: scenario not found: {scenario_filter}", file=sys.stderr)
            return 1

    print("Science benchmark run plan")
    print("--------------------------")
    for scenario in selected:
        setup = scenario["setup"]
        run = scenario["run"]
        world = setup["world"]
        sid = scenario["id"]
        print(f"\n[{sid}] {scenario['title']}")
        print(
            f"replicates: {run['replicates']}, ticks: {run['ticks']}, warmup_ticks: {run['warmup_ticks']}"
        )
        cmd = (
            f"FEROX_SCENARIO={sid} ./scripts/run.sh server "
            f"-w {world['width']} -H {world['height']} -c {setup['initial_colonies']} "
            f"-t {run['threads']} -r {run['tick_rate_ms']}"
        )
        print(f"server_cmd: {cmd}")
        print(
            "analysis_note: collect metrics from world snapshots and compare to pass_band values"
        )

    return 0


def list_scenarios(data: dict) -> int:
    for scenario in data.get("scenarios", []):
        print(f"{scenario.get('id')}\t{scenario.get('title')}")
    return 0


def parse_args() -> argparse.Namespace:
    repo_root = pathlib.Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(
        description="Ferox science benchmark scenario utility"
    )
    parser.add_argument(
        "--config",
        default=str(repo_root / "config" / "science_benchmarks.json"),
        help="path to benchmark scenario config JSON",
    )

    subparsers = parser.add_subparsers(dest="command", required=True)

    subparsers.add_parser("list", help="list configured scenarios")

    validate_parser = subparsers.add_parser("validate", help="validate scenario config")
    validate_parser.add_argument(
        "--strict", action="store_true", help="require canonical scenario set"
    )

    plan_parser = subparsers.add_parser(
        "run-plan", help="print runnable server command plan"
    )
    plan_parser.add_argument("--scenario", default="all", help="scenario id or 'all'")

    return parser.parse_args()


def main() -> int:
    args = parse_args()
    config_path = pathlib.Path(args.config)

    try:
        data = load_config(config_path)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    if args.command == "list":
        return list_scenarios(data)

    if args.command == "validate":
        errors = validate_config(data, strict=args.strict)
        if errors:
            print("science benchmark config invalid:", file=sys.stderr)
            for err in errors:
                print(f"- {err}", file=sys.stderr)
            return 1
        print(f"science benchmark config valid: {config_path}")
        return 0

    if args.command == "run-plan":
        return print_run_plan(data, args.scenario)

    print("error: unknown command", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
