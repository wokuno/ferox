# Ferox Benchmark Runbook

This runbook defines a repeatable process for measuring Ferox performance
without chasing noisy one-off results.

## Goals

- Keep measurements reproducible on the same host.
- Track both throughput and tail latency style metrics.
- Compare changes with stable command lines and workloads.

## Standard Benchmark Commands

- Threadpool microbench:
  - `./build/tests/test_threadpool_microbench`
- Threadpool profile scan:
  - `./build/tests/test_threadpool_profile_scan`
- Performance profile suite:
  - `./build/tests/test_performance_profile`
- Unit-level world diagnostics:
  - `./build/tests/test_perf_unit_world`
- Unit-level protocol diagnostics:
  - `./build/tests/test_perf_unit_protocol`
- Component-level atomic diagnostics:
  - `./build/tests/test_perf_component_atomic`
- Hardware profile inspection:
  - `./build/src/server/ferox_server --print-hardware`
- Focused ctest suite:
  - `ctest --test-dir build --output-on-failure -R "ThreadpoolMicrobenchTests|ThreadpoolProfileScanTests|PerformanceProfilingTests|PerfUnitWorldTests|PerfUnitProtocolTests|PerfComponentAtomicTests|ThreadpoolStressTests|SimulationLogicTests"`
- Jitter-reduced summary (multi-iteration):
  - `./scripts/perf_multi_iter.py -n 7 --profile balanced`

## Granularity Ladder

- `unit`:
  - `test_perf_unit_world` (lookup and world churn micro-cost)
  - `test_perf_unit_protocol` (RLE grid codec + chunked grid transport throughput/ratio)
- `component`:
  - `test_perf_component_atomic` (atomic tick pipeline modes)
  - `test_threadpool_profile_scan` (scheduler profile behavior)
- `system`:
  - `test_threadpool_microbench` (cross-scenario scheduler matrix)
  - `test_performance_profile` (end-to-end hotspots, including snapshot build cost)

## Environment Guidance

- Build in `Release` mode.
- Avoid running unrelated heavy workloads while measuring.
- Run each benchmark at least 3 times and compare medians.
- Keep worker counts and benchmark env vars constant across runs.

Optional threadpool tuning env vars:

- `FEROX_ACCELERATOR` (`auto|cpu|apple|amd`)
- `FEROX_THREADPOOL_PROFILE` (`latency|throughput|balanced`)
- `FEROX_THREADPOOL_FASTQ_CAPACITY`
- `FEROX_THREADPOOL_FASTQ_SHARDS` (`0` = auto)
- `FEROX_THREADPOOL_FASTQ_BATCH1`
- `FEROX_THREADPOOL_FASTQ_STEAL_PROBES`
- `FEROX_THREADPOOL_STEAL_PROBES`
- `FEROX_THREADPOOL_STEAL_BATCH`
- `FEROX_THREADPOOL_FUSE_MIN_BATCH`
- `FEROX_THREADPOOL_IDLE_SPINS`
- `FEROX_THREADPOOL_TELEMETRY`
- `FEROX_THREADPOOL_MICROBENCH_MODE`

Optional atomic simulation tuning env vars:

- `FEROX_ATOMIC_SERIAL_INTERVAL` (default `5`) controls how often expensive serial maintenance
  phases (mutate/divide/recombine + atomic resync) run inside `atomic_tick`.
- `FEROX_ATOMIC_FRONTIER_DENSE_PCT` (default `15`) disables frontier scheduling when
  active source density exceeds this percentage of total grid cells.

Accelerator target guidance:

- `FEROX_ACCELERATOR=cpu`
  - use when benchmarking CPU-only hosts or forcing baseline behavior.
- `FEROX_ACCELERATOR=apple`
  - use on Apple Silicon when you want the Apple-oriented host tuning profile.
- `FEROX_ACCELERATOR=amd`
  - use on AMD GPU hosts when you want the throughput-oriented host tuning profile.
- `FEROX_ACCELERATOR=auto`
  - use for normal runs; Ferox probes the host and selects the best available target.

Note: the current runtime selection layer tunes the CPU atomic backend. It does
not yet switch the simulation loop to a dedicated Metal, HIP, or OpenCL kernel.

Recommended tiny-task profile on this x86_64 host (from benchmark sweep):

- `FEROX_THREADPOOL_FASTQ=1`
- `FEROX_THREADPOOL_FUSE_MIN_BATCH=4`
- `FEROX_THREADPOOL_EXEC_BATCH=16`
- `FEROX_THREADPOOL_STEAL_BATCH=4`
- `FEROX_THREADPOOL_STEAL_PROBES=4`
- `FEROX_THREADPOOL_IDLE_SPINS=64`

Use these for tiny-task heavy workloads. For general simulation correctness-sensitive runs,
defaults are conservative and avoid aggressive fusion.

Profile presets are tuned as follows:

- `FEROX_THREADPOOL_PROFILE=latency`
  - tuned for best batch-1 responsiveness while keeping good batch-8/32 throughput.
  - preset values: `FASTQ=1`, `FASTQ_BATCH1=1`, `FASTQ_SHARDS=1`, `FASTQ_STEAL_PROBES=1`, `STEAL_PROBES=4`, `STEAL_BATCH=4`, `EXEC_BATCH=16`, `FUSE_MIN_BATCH=8`, `IDLE_SPINS=64`.
- `FEROX_THREADPOOL_PROFILE=throughput`
  - tuned for heavier batch-8/32 throughput workloads.
  - preset values: `FASTQ=1`, `FASTQ_BATCH1=1`, `FASTQ_SHARDS=1`, `FASTQ_STEAL_PROBES=4`, `STEAL_PROBES=8`, `STEAL_BATCH=8`, `EXEC_BATCH=32`, `FUSE_MIN_BATCH=4`, `IDLE_SPINS=32`.
- `FEROX_THREADPOOL_PROFILE=balanced`
  - general-purpose profile used by profiling tests.
  - preset values: `FASTQ=1`, `FASTQ_BATCH1=1`, `FASTQ_SHARDS=1`, `FASTQ_STEAL_PROBES=1`, `STEAL_PROBES=8`, `STEAL_BATCH=4`, `EXEC_BATCH=32`, `FUSE_MIN_BATCH=4`, `IDLE_SPINS=32`.

## Regression Check Pattern

1. Run baseline on current `main`.
2. Run candidate change with identical commands.
3. Compare:
   - microbench `MICROBENCH_SUMMARY`
   - profile lines:
     - `[threadpool tiny tasks]`
     - `[atomic_spread_step]`
     - `[atomic_tick]`
4. Only claim win when multiple runs point in same direction.

## Profiling Workflow

- Use `scripts/profile.sh` for a capture wrapper around Linux `perf`.
- Use `scripts/profile_c2c.sh` to inspect false-sharing/cacheline contention hot spots.
- Start with CPU flamegraph-style capture, then inspect lock and cache contention.

## Artifact Export

- Use `scripts/benchmark_export.sh` to collect benchmark + ctest outputs into a timestamped artifact directory.
- This output is suitable for nightly baseline diffs and historical tracking.
