# Ferox Performance Targets

This document defines the repeatable rebaseline workflow and the target metrics
we track for the default `400x200` / `50` colony profile.

## Default-Profile Rebaseline Workflow

Run these steps on a quiet host and keep the same machine class across
comparisons.

```bash
./scripts/build.sh release
python3 scripts/perf_scenarios.py --build-types Release --scales 2 --repeats 3
ctest --test-dir build --output-on-failure -R "PerformanceComponentTests|PerformanceProfilingTests"
```

Use the generated `artifacts/perf/<timestamp>/` directory as the source of truth
for raw logs, summary rows, and run extras.

Checked-in machine-readable baseline:

- `config/perf_baseline_default_profile.json`
- generated from `python3 scripts/perf_baseline_report.py artifacts/perf/<timestamp>`

## Metrics To Rebaseline

- broad tick cost on the default profile:
  - `simulation_tick (serial)`
  - `atomic_tick (1 thread)`
  - `atomic_tick (2 threads)`
  - `atomic_tick (4 threads)`
  - `atomic/serial time ratio`
- transport and broadcast cost on the default profile:
  - `broadcast build snapshot`
  - `broadcast build+serialize`
  - `broadcast end-to-end (0 clients)`
  - average snapshot size
  - average encoded size
- scheduler overhead:
  - `threadpool tiny tasks`
  - `threadpool chunked submit`
  - `threadpool batched tasks`
  - `tiny/batched ratio`
- focused corroboration:
  - `server snapshot build`
  - protocol serialize/deserialize throughput
  - atomic phase share and sync throughput lines

## Acceptance Shape

- prefer median-of-3 capture over single-run best-case numbers
- keep CI guardrails loose enough for runner variance and enforce them on ratios,
  medians, and sizes rather than exact nanosecond deltas
- treat larger snapshot/encoded payloads as transport regressions even when wall
  time is noisy
- keep focused component and profiling tests green before updating stored target
  numbers

## Current Default-Profile Baseline

Source artifact: `artifacts/perf/20260319-132255`

- `simulation_tick (serial)`: `441.52 ms`
- `atomic_tick (1 thread)`: `122.70 ms`
- `atomic_tick (2 threads)`: `107.82 ms`
- `atomic_tick (4 threads)`: `150.31 ms`
- `broadcast build snapshot`: `3.98 ms`
- `broadcast build+serialize`: `8.67 ms`
- `broadcast end-to-end (0 clients)`: `8.74 ms`
- `server snapshot build`: `8.32 ms`
- `protocol serialize+deserialize`: `9.65 ms`
- `threadpool tiny tasks`: `14.49 ms`
- `threadpool chunked submit`: `8.91 ms`
- `threadpool batched tasks`: `0.66 ms`
- `atomic/serial time ratio`: `0.6567x`
- `tiny/batched ratio`: `21.7367x`
- average transport payloads from the same run: snapshot `159.96 KiB`, encoded `4.82 KiB`

## Source Files

- `docs/PERFORMANCE.md` stores the human-readable baseline notes
- `docs/PERF_RUNBOOK.md` stores the operator workflow
- `docs/TESTING.md` stores the validation ladder
- `docs/PROGRESS.md` tracks the active rebaseline workstream
