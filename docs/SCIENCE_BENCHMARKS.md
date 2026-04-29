# Science Benchmark Scenarios

This document defines canonical science-facing benchmark scenarios used to validate model behavior trends and guard against silent drift.

Scenario definitions live in `config/science_benchmarks.json` and are managed with `scripts/science_benchmarks.py`.

## Current Status

The current benchmark utility validates the scenario catalog and prints a run
plan. It does not yet execute scenarios, collect metrics, or enforce pass bands
end-to-end. That execution gap is tracked by GitHub issue `#109`.

The April 2026 audit also found that some documented CTest targets can match
zero tests. Keep `#173` closed-loop before treating CI science checks as
authoritative.

## Scenario Catalog

The following scenarios are required and validated in strict mode:

1. `nutrient_limited_growth`
2. `toxin_duel`
3. `quorum_activation_wave`
4. `persistence_stress_response`
5. `frontier_genetic_drift`
6. `coexistence_dynamics`

Each scenario includes:
- fixed setup inputs (`world` dimensions, `initial_colonies`, `seed`)
- run settings (`ticks`, `replicates`, `warmup_ticks`, `tick_rate_ms`, `threads`)
- at least two measurable metrics with explicit pass bands (`pass_band.min`, `pass_band.max`)

## Metrics and Tolerances

Each metric is a bounded expectation window, not a single-point target.

- **Why pass bands:** simulation behavior is stochastic; validating with ranges catches regressions while allowing natural variance.
- **Interpreting failures:** values outside pass bands indicate likely model drift, RNG/initialization changes, or a behavior regression.
- **Tolerance guidance:** tune bands only when there is a scientifically justified model change and update this document in the same PR.
- **Calibration provenance:** pass bands should record baseline seed count, seed set, mean, standard deviation, quantiles, confidence interval, acceptable effect size, calibration date, commit, platform, and benchmark tool version.
- **Replay boundary:** use exact replay only for tiny deterministic fixtures; use distribution comparisons for stochastic science scenarios.

Current canonical metric groups by scenario:

- `nutrient_limited_growth`: `population_peak_fraction`, `late_stage_population_slope`
- `toxin_duel`: `boundary_switch_rate`, `winner_peak_toxin_mean`
- `quorum_activation_wave`: `quorum_activation_latency_ticks`, `active_signal_front_speed`
- `persistence_stress_response`: `dormant_fraction_at_peak_stress`, `post_stress_recovery_ticks`
- `frontier_genetic_drift`: `frontier_core_genome_distance`, `lineage_retention_half_life_ticks`
- `coexistence_dynamics`: `effective_diversity_shannon`, `survivor_count_at_end`

## Automation Entrypoints

### Validate scenario configuration

```bash
python3 scripts/science_benchmarks.py validate --strict
```

Expected CTest target: `ScienceBenchmarkConfigTests`.

Audit note: verify this target with `ctest -N -R ScienceBenchmarkConfigTests`
after CMake changes. Issue `#173` tracks no-op target/filter hardening.

### List scenarios

```bash
python3 scripts/science_benchmarks.py list
```

### Generate run plan commands

```bash
python3 scripts/science_benchmarks.py run-plan
python3 scripts/science_benchmarks.py run-plan --scenario toxin_duel
```

The run plan emits scenario-specific `scripts/run.sh server ...` command lines and indicates which metrics must be compared to pass bands.

## Scenario Setup Notes

- Keep `seed` fixed for baseline comparability across branches.
- Use explicit replicate tiers:
  - PR smoke: 8-24 fixed seeds for no-obvious-drift checks.
  - Nightly: 100+ seeds for stable distribution summaries.
  - Release/rebaseline: 500-1000+ seeds where runtime permits.
- Keep `warmup_ticks` below 15% of total ticks unless the scenario is explicitly focused on long transient behavior.
- If you change world dimensions or initial colony counts, reassess pass bands for all metrics in that scenario.
- Prefer behavioral metrics over screenshots or single-seed snapshots: frontier roughness, compactness, active-shell thickness, lineage entropy, sector count, extinction rate, takeover probability, and recovery time.

## Research Alignment

Benchmark definitions should move toward ODD/MIASE-style experiment metadata:
`phenomenon`, `hypothesis`, `mechanics_exercised`, `metric_formula`,
`sampling_window`, `baseline_provenance`, `runtime_tier`, `artifact_schema`,
and `literature_refs`.

The broader research rationale is maintained in
[Biology And Simulation Research Notes](BIOLOGY_SIMULATION_RESEARCH.md).
Issue `#175` tracks the sensitivity-analysis lane and calibrated pass-band
provenance work.

## CI and Local Checks

- Local quick check: `./scripts/test.sh science`
- Full local validation: `python3 scripts/science_benchmarks.py validate --strict`

Both commands validate the scenario schema and canonical coverage; they fail fast on malformed fields, invalid pass bands, duplicate IDs, or missing required scenarios.
