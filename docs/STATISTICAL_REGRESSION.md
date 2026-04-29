# Statistical Regression Testing

This document defines the simulation statistical regression check added to CI for issue #57.

Audit note, 2026-04-29: issue `#173` tracks a CTest wiring problem where
documented test filters can match zero tests. Re-check
`ctest -N -R SimulationStatRegressionTests` whenever this target or CI wiring
changes.

## Goal

Detect behavior drift in simulation outcomes without brittle one-seed exact snapshots.

## Test Design

- Test executable: `tests/test_simulation_stat_regression.c`
- Intended CTest target: `SimulationStatRegressionTests`
- Batch configuration:
  - 24 fixed seeds
  - 96x64 world
  - 14 initial colonies
  - 90 ticks per seed
- For each seed, we collect three metrics:
  - `occupied_ratio`: occupied cells / total world cells
  - `active_colonies`: active colonies with non-zero grid occupancy
  - `dominant_share`: largest colony cells / total occupied cells

The test computes distribution summaries for each metric (mean, stddev, min, max) and asserts mean/stddev against baseline thresholds.

## Threshold Rationale

Thresholds are anchored to current `origin/main` behavior and tuned to catch meaningful shifts while allowing normal stochastic spread:

- `occupied_ratio`: baseline mean `0.7921`, tolerance `+/- 0.0800`; baseline stddev `0.0579`, tolerance `+/- 0.0400`
- `active_colonies`: baseline mean `24.0000`, tolerance `+/- 4.0000`; baseline stddev `11.5253`, tolerance `+/- 4.0000`
- `dominant_share`: baseline mean `0.4007`, tolerance `+/- 0.0800`; baseline stddev `0.1025`, tolerance `+/- 0.0500`

These tolerances are intentionally wider than one-run noise because the simulation is stochastic and occasionally exhibits long-tail seed outcomes.

Current provenance: refreshed on 2026-04-29 when the test was wired into CTest
under issue `#173`, using the fixed 24-seed set in `test_simulation_stat_regression.c`
on the local macOS Release build.

Future rebaselines should record provenance beside the thresholds: seed set,
seed count, baseline commit, platform, compiler, build type, mean, standard
deviation, min/max, quantiles, confidence interval, and the accepted effect
size. This keeps pass bands as calibrated statistical contracts instead of
hand-tuned ranges.

## Anti-Flake Strategy

The anti-flake approach is built into the measurement method:

- Multi-seed aggregation (24 seeds) reduces any single outlier seed impact.
- Distribution checks use both center (`mean`) and spread (`stddev`) instead of single-value snapshots.
- Fixed seed set keeps the test reproducible and debuggable across hosts.
- CI publishes metric summaries so drift can be triaged before tightening thresholds.

## CI Integration

`ci.yml` should run `SimulationStatRegressionTests` in both macOS and
self-hosted Linux build jobs, then emit summary metrics in the GitHub Actions
job summary. Issue `#173` tracks verification that the target is actually
registered and selected.

The test logs machine-parseable lines in this format:

`STAT_REGRESSION metric=<name> mean=<v> stddev=<v> min=<v> max=<v> expected_mean=<v> mean_tol=<v> expected_stddev=<v> stddev_tol=<v>`

This gives pass/fail and observability without relying on fragile golden output files.

## Relationship To Science Benchmarks

Statistical regression is a project baseline check. It answers whether Ferox
behavior drifted from the current accepted implementation.

Science validation is broader. It should compare benchmark distributions to
literature-backed qualitative or quantitative patterns and should use the
scenario/provenance guidance in [Science Benchmark Scenarios](SCIENCE_BENCHMARKS.md)
and [Biology And Simulation Research Notes](BIOLOGY_SIMULATION_RESEARCH.md).
