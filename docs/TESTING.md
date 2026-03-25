# Ferox Testing Guide

This document describes the current CMake/CTest-based test matrix and how we
run correctness and performance diagnostics.

## Test Matrix

Ferox currently defines **28 CTest targets** (see `ctest -N`):

- Phase suites: `Phase1Tests` .. `Phase6Tests`
- Advanced correctness/stability: `GeneticsAdvancedTests`, `WorldAdvancedTests`,
  `SimulationLogicTests`, `VisualStabilityTests`, `CombatSystemTests`, `GuiTests`,
  `ClientLogicSurfaceTests`
- Stress and edge coverage: `SimulationStressTests`, `ThreadpoolStressTests`,
  `ProtocolEdgeTests`, `NamesExhaustiveTests`, `ColorsExhaustiveTests`,
  `WorldBranchCoverageTests`, `ServerBranchCoverageTests`
- Runtime detection coverage: `HardwareProfileTests`
- Performance diagnostics:
  - `PerformanceProfilingTests`
  - `ThreadpoolMicrobenchTests`
  - `ThreadpoolProfileScanTests`
  - `PerfUnitWorldTests`
  - `PerfUnitProtocolTests`
  - `PerformanceComponentTests`
- Aggregated runner: `AllTests`

Sources are in `tests/` and wired in `tests/CMakeLists.txt`.

## Build and Run

```bash
# Configure + build (default Release)
./scripts/build.sh release

# Run everything
ctest --test-dir build --output-on-failure

# Run quick correctness slice
ctest --test-dir build --output-on-failure -R "Phase|SimulationLogicTests|ProtocolEdgeTests"

# Run client-facing correctness slice
ctest --test-dir build --output-on-failure -R "ClientLogicSurfaceTests|Phase5Tests|Phase6Tests"

# Run perf-focused + hardware slice
ctest --test-dir build --output-on-failure -R "HardwareProfileTests|ThreadpoolMicrobenchTests|ThreadpoolProfileScanTests|PerformanceProfilingTests|PerfUnitWorldTests|PerfUnitProtocolTests|PerformanceComponentTests"
```

You can also use helper categories in `scripts/test.sh`, for example:

```bash
./scripts/test.sh all
./scripts/test.sh quick
./scripts/test.sh phase3
./scripts/test.sh coverage
```

## Performance Test Ladder

Performance work is validated at three levels:

- `unit`
  - `test_perf_unit_world`
  - `test_perf_unit_protocol`
- `component`
  - `test_perf_components`
  - `test_threadpool_profile_scan`
- `system`
  - `test_threadpool_microbench`
  - `test_performance_profile` (includes architecture-specific atomic-cost lane output)

For jitter-resistant analysis, run multi-iteration medians:

```bash
python3 scripts/perf_scenarios.py --build-types Release --scales 2 --repeats 3
```

See `docs/PERF_RUNBOOK.md` and `docs/PERF_TARGETS.md` for required commands and
acceptance thresholds.

For the default `400x200` / `50` colony profile rebaseline, run this validation
ladder in order:

```bash
./scripts/build.sh release
python3 scripts/perf_scenarios.py --build-types Release --scales 2 --repeats 3
ctest --test-dir build --output-on-failure -R "PerformanceComponentTests|PerformanceProfilingTests"
```

This sequence keeps the broad repeated-run numbers and the focused hotspot
diagnostics aligned on the same branch and build settings.

## Environment Knobs Used in Tests

- Threadpool profile presets:
  - `FEROX_THREADPOOL_PROFILE=latency|throughput|balanced`
- Microbench mode:
  - `FEROX_THREADPOOL_MICROBENCH_MODE=report`
- Atomic serial cadence:
  - `FEROX_ATOMIC_SERIAL_INTERVAL` (default `5`)
- Frontier dense cutoff:
  - `FEROX_ATOMIC_FRONTIER_DENSE_PCT` (default `15`)
- Accelerator target selection:
  - `FEROX_ACCELERATOR=auto|cpu|apple|amd`
- Perf profiling loop scale:
  - `FEROX_PERF_SCALE=1..10` for the atomic-cost lane in `test_performance_profile`

CTest sets several of these automatically for perf targets in
`tests/CMakeLists.txt`.

## Debugging Failures

```bash
# Rebuild with sanitizers
./scripts/build.sh sanitize clean

# Run a single failing test verbosely
ctest --test-dir build --output-on-failure -V -R SimulationLogicTests
```

For deeper inspection, build a debug tree and run under `gdb`, `valgrind`, or
`perf` as needed.

## Expectations for New Work

- Add correctness coverage with each behavior change.
- Add/update perf diagnostics for hot-path changes.
- Evaluate perf claims using medians across repeated runs.
- Keep test names and CTest labels consistent with existing conventions.

Recent protocol/client correctness coverage now also includes structured command
feedback checks for manual spawn acceptance/rejection in:

- `ProtocolEdgeTests`
- `ServerBranchCoverageTests`
- `Phase6Tests`

That same coverage now also checks `CMD_SELECT_COLONY` accept/reject/clear
feedback so selection failures no longer remain silent in the command surface.

That command-status coverage now also checks `CMD_RESET` acceptance so reset
rebuilds no longer rely on optimistic client state or later snapshots alone in:

- `ProtocolEdgeTests`
- `ServerBranchCoverageTests`
- `Phase6Tests`
