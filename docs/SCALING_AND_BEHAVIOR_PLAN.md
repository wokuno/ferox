# Scaling and Behavior Plan

This note captures the current research-backed direction for larger default
worlds, richer colony behavior, and the follow-on work now tracked in GitHub.

## What Changed Now

- default server workload increased to `400x200` with `50` initial colonies
- `scripts/run.sh` now mirrors the larger world profile and defaults to auto
  thread selection instead of forcing `4`
- the active atomic runtime now refreshes behavior layers every tick and updates
  colony lifecycle state, signaling, biofilm, drift, and learning dynamics
- signal, alarm, and horizontal gene-transfer systems are now exercised in the
  simulation instead of remaining mostly dormant data-model features
- world broadcast prep no longer rescans the full grid once per colony; grid
  export and colony centroid accumulation now happen in a single pass
- large worlds above the inline snapshot threshold now stream grid chunks over
  `MSG_WORLD_DELTA` instead of silently dropping grid data
- client-side large-world rendering now avoids per-cell linear colony scans in
  the GUI grid path

## Key Findings From Parallel Research

- larger defaults were mostly blocked by transport and broadcast bookkeeping,
  not just raw spread throughput
- the live server uses `atomic_tick()`, so behavior systems that only lived in
  `simulation_tick()` were not visible during normal runtime
- many genome/state fields already existed and could be activated with modest
  code changes: signaling, alarms, dormancy resistance, motility, specialization,
  and gene transfer

## Tracking Issues

- `#89` Define and validate a larger default world profile
- `#92` Restore colony ecology behavior parity on the atomic runtime path
- `#87` Implement signal, alarm, and horizontal gene-transfer behaviors
- `#91` Remove per-colony full-grid rescans from world broadcast path
- `#88` Scale world-state transport for larger maps and colony counts
- `#90` Define a backend interface for future CPU/GPU simulation execution
- `#97` Add dirty-tile AtomicWorld sync to avoid full-grid copies each tick
- `#99` Isolate slow clients with queued/coalesced world broadcasts
- `#100` Unify RNG sources and add exact replay fixtures
- `#101` Wire Monod uptake and EPS-dependent transport into the live runtime
- `#103` Split atomic serial maintenance cadence by freshness requirements
- `#107` Remove redundant chunk build/copy work from the large-world broadcast path
- `#109` Execute configured benchmark scenarios and enforce pass bands in CI
- `#110` Reconcile the protocol spec with the live wire format and version future changes
- `#143` Rebaseline performance and transport costs for the new default world profile

## Validation Focus

- correctness: `ctest --test-dir build --output-on-failure -R "SimulationLogicTests|Phase3Tests|HardwareProfileTests"`
- atomic behavior/perf: `ctest --test-dir build --output-on-failure -R "PerfComponentAtomicTests|PerformanceProfilingTests"`
- hardware report: `./build/src/server/ferox_server --print-hardware`

## Next Recommended Moves

- rebaseline perf targets for the new `400x200` / `50` colony default profile (`#143`)
- tighten snapshot-build and chunked-transport perf guardrails with target values (`#107`, `#127`, `#128`)
- continue reducing GUI and terminal client render overhead on dense large worlds (`#99`)
- keep the CPU atomic backend as reference while defining the future accelerator backend seam (`#90`)
- land follow-up ecology/runtime work with docs updates in the same PRs (`#101`, `#103`, `#105`)
  backend seam
