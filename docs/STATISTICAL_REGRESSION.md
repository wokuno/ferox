# Statistical Regression Testing

This document defines the simulation statistical regression check added to CI for issue #57.

## Goal

Detect behavior drift in simulation outcomes without brittle one-seed exact snapshots.

## Test Design

- Test executable: `tests/test_simulation_stat_regression.c`
- CTest target: `SimulationStatRegressionTests`
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

- `occupied_ratio`: baseline mean `0.954`, tolerance `+/- 0.060`; baseline stddev `0.032`, tolerance `+/- 0.030`
- `active_colonies`: baseline mean `21.0`, tolerance `+/- 3.0`; baseline stddev `3.4`, tolerance `+/- 1.8`
- `dominant_share`: baseline mean `0.171`, tolerance `+/- 0.050`; baseline stddev `0.038`, tolerance `+/- 0.025`

These tolerances are intentionally wider than one-run noise because the simulation is stochastic and occasionally exhibits long-tail seed outcomes.

## Anti-Flake Strategy

The anti-flake approach is built into the measurement method:

- Multi-seed aggregation (24 seeds) reduces any single outlier seed impact.
- Distribution checks use both center (`mean`) and spread (`stddev`) instead of single-value snapshots.
- Fixed seed set keeps the test reproducible and debuggable across hosts.
- CI publishes metric summaries so drift can be triaged before tightening thresholds.

## CI Integration

`ci.yml` runs `SimulationStatRegressionTests` in both macOS and self-hosted Linux build jobs, then emits summary metrics in the GitHub Actions job summary.

The test logs machine-parseable lines in this format:

`STAT_REGRESSION metric=<name> mean=<v> stddev=<v> min=<v> max=<v> expected_mean=<v> mean_tol=<v> expected_stddev=<v> stddev_tol=<v>`

This gives pass/fail and observability without relying on fragile golden output files.

## Related Tracking

- `#100` Unify RNG sources and add exact replay fixtures to complement the statistical lane with exact deterministic checks.
- `#109` Execute configured benchmark scenarios and enforce pass bands in CI for broader science-facing validation.
- Keep this document updated alongside `docs/SCIENCE_BENCHMARKS.md`, `docs/TESTING.md`, and any replay/test workflow changes.
