# Ferox Performance Evaluation

This document captures how to run performance measurements, current baseline data, and bottlenecks found in the current implementation.

## How to Run Performance Tests

```bash
# Fast run
./scripts/test.sh perf

# Heavier run (recommended for comparisons)
FEROX_PERF_SCALE=5 ./scripts/test.sh perf

# Target only the perf eval binary (if you want focused output)
ctest --output-on-failure -R PerformanceEvalTests -V
```

`FEROX_PERF_SCALE` increases loop counts in `test_performance_eval.c` so timings are more stable.

When comparing branches, use the same `FEROX_PERF_SCALE`, machine class, and run count.
For regression checks, run at least 3 times and compare medians instead of single-run minima.

## CI/Coverage Caveats

- The coverage workflow excludes `PerformanceEvalTests` and `AllTests` (`ctest -E "(PerformanceEvalTests|AllTests)"`).
- Coverage builds use `-DENABLE_COVERAGE=ON`, which changes timing characteristics.
- Performance thresholds in `test_performance_eval.c` are intentionally loose for host variance and instrumentation overhead.
- For comparative performance work, run `./scripts/test.sh perf` on a stable local machine with the same `FEROX_PERF_SCALE`.

## 2026-03-19 Default-Profile Rebaseline Procedure

Issue `#143` resets the default-profile baseline around the live launcher shape:
`400x200` world, `50` initial colonies, release build, and repeated runs.

Recommended capture flow:

```bash
./scripts/build.sh release
python3 scripts/perf_scenarios.py --build-types Release --scales 2 --repeats 3
ctest --test-dir build --output-on-failure -R "PerformanceComponentTests|PerformanceProfilingTests"
```

Capture and compare these default-profile metrics:

- broad tick cost: `simulation_tick (serial)`, `atomic_tick (1/2/4 threads)`, and `atomic/serial time ratio`
- transport cost: `broadcast build snapshot`, `broadcast build+serialize`, `broadcast end-to-end (0 clients)`
- transport payload size: average snapshot KiB and encoded KiB
- scheduler overhead: `threadpool tiny tasks`, `threadpool chunked submit`, `threadpool batched tasks`, and `tiny/batched ratio`
- corroborating focused metrics: `server snapshot build`, protocol throughput, atomic phase share, and sync throughput lines

Use the generated `artifacts/perf/<timestamp>/` directory as the raw evidence and
update `docs/PERF_TARGETS.md` plus the checked-in baseline data when the medians
have been confirmed.

## 2026-03-19 Default-Profile Rebaseline Results

Confirmed release medians from `artifacts/perf/20260319-172155/` for the default
`400x200` / `50` colony profile (`FEROX_PERF_SCALE=2`, `3` repeats):

- `simulation_tick (serial)`: **457.65 ms**
- `atomic_tick (1 thread)`: **122.50 ms**
- `atomic_tick (2 threads)`: **107.12 ms**
- `atomic_tick (4 threads)`: **151.92 ms**
- atomic/serial time ratio: **0.63x**
- `broadcast build snapshot`: **3.99 ms**
- `broadcast build+serialize`: **8.51 ms**
- `broadcast end-to-end (0 clients)`: **8.50 ms**
- `server snapshot build`: **8.15 ms**
- `protocol serialize+deserialize`: **9.47 ms**
- `threadpool tiny tasks`: **23.74 ms**
- `threadpool batched tasks`: **0.72 ms**
- tiny/batched ratio: **41.28x**

Transport payload snapshot from the same pass:

- average snapshot payload: about **159.96 KiB**
- average encoded payload: about **4.82 KiB**

Interpretation:

- The new default profile is now explicitly captured in checked-in baseline data
  via `config/perf_baseline_default_profile.json`.
- Broad transport costs are modest relative to the serial tick on this host.
- The scheduler still shows a clear tiny-task overhead signal even after the
  worker follow-on submit fast path merged, so follow-up work should keep
  batching/coarser work units in scope for external submit-heavy lanes.
- `atomic_tick (2 threads)` currently beats `4 threads)` on this host, which is a
  reminder not to assume the largest worker count is the best default-profile lane.

## 2026-03-19 Hot Shared Struct Guardrails

Low-risk cacheline guardrails now live in shared headers for the most contended scheduler and atomic-spread metadata:

- `src/shared/cacheline.h` defines the common cacheline target plus reusable static-assert helpers.
- `ThreadPoolHotCounters`, `AtomicSpreadSharedState`, and `AtomicPhaseSharedState` are explicitly padded to one cacheline and checked with compile-time offset assertions.
- `tests/test_threadpool_stress.c` verifies the exposed hot structs stay cacheline-sized and that the aligned members land on cacheline boundaries in the owning runtime structs.
- `tests/test_performance_profile.c` now prints the owning-struct member offsets plus size data for those hot structs beside the existing `AtomicColonyStats` platform report so local perf runs can confirm the host-specific layout quickly.

This change is intentionally structural only: it does not alter scheduling policy, memory-order semantics, or feature defaults.

## 2026-03-19 Atomic Order Audit Scope

Issue `#115` is scoped as a docs-first audit of the existing C11 atomic sites,
with only low-risk code follow-ups allowed.

- Audit target fields: `AtomicCell.colony_id`, `AtomicCell.age`,
  `AtomicColonyStats.cell_count`, `AtomicColonyStats.max_cell_count`, and
  `World.next_colony_id`.
- Expected result: replace implicit default atomics with explicit orders only
  where the existing invariant is already relaxed-order safe, and keep acquire
  semantics only on true wait/wake polling paths.
- Out of scope for this pass: algorithm changes, lock removal, converting
  mutex-protected counters into atomics, or broad perf retuning.
- Validation target: focused correctness tests plus the existing perf/profile
  suite so the change stays CI-friendly.

Completed implementation notes:

- `src/shared/atomic_types.h` now uses explicit relaxed load/store/RMW/CAS calls
  for the ownership, age, and colony-stat fields that already depend only on
  uniqueness or numeric integrity.
- `src/server/world.c` now uses explicit relaxed ordering for
  `World.next_colony_id`, reflecting that the id allocator provides uniqueness,
  not publication of the rest of the colony payload.
- Focused regression coverage now includes a relaxed-order spread/stat roundtrip
  check in `test_simulation_logic` and a monotonic-id check in
  `test_world_branch_coverage`.

## Current Baseline (macOS arm64, scenario runs on 2026-03-05)

Fresh scenario runs were executed with:

```bash
python3 scripts/perf_scenarios.py --build-types Debug Release --scales 1 2 --repeats 2
```

Key observations from `artifacts/perf/20260305-133809/`:

- Release:
  - atomic/serial ratio: **1.52x**
  - tiny/batched threadpool ratio: **4.07x**
- Debug:
  - atomic/serial ratio: **1.64x**
  - tiny/batched threadpool ratio: **4.96x**

Selected medians:

- `debug_scale2` `atomic_tick (1 thread)`: **2715.78 ms**
- `debug_scale2` `atomic_tick (2 threads)`: **1658.02 ms**
- `debug_scale2` `atomic_tick (4 threads)`: **1379.82 ms**
- `debug_scale2` `simulation_tick (serial)`: **1232.58 ms**

Interpretation:

- The atomic path is still structurally slower than serial for the current grid sizes.
- Small-task scheduling overhead remains a first-order cost.
- One near-term improvement is to avoid oversharding the atomic region decomposition for 1-thread and 2-thread runs.

## Previous Baseline (macOS arm64, 3 runs, `FEROX_PERF_SCALE=5`)

Average values from repeated runs:

- SIMD toxin decay ratio (`optimized/scalar`): **1.53x** (scalar faster on NEON; no regression)
- Name + color generation: **39.6 ms** for 250k iterations
- World create/init/destroy: **32.8 ms** for 900 iterations
- Protocol serialize+deserialize: **24.0 ms** for 600 iterations (**630.6 MB/s**)
- Broadcast path breakdown metrics now emitted (`build snapshot`, `build+serialize`, `end-to-end (0 clients)`, plus stage-share percentages)
- `simulation_tick` (serial): **533 ms** for 175 ticks (~3.0 ms/tick)
- `atomic_tick` (4 threads): **1299 ms** for 150 ticks (~8.7 ms/tick)
- Atomic vs serial ratio: **2.19x** (atomic path still slower; see notes)
- Threadpool submit+execute: **37.6 ms** for 250k tasks

## New Phase + Sync Metrics

`PerformanceEvalTests` now emits additional `[perf]` lines for:

- `atomic phase: age`
- `atomic phase: spread`
- `atomic phase: sync_to_world`
- `atomic phase: serial core`
- `atomic phase: sync_from_world`
- `atomic phase share: ...` (percentage split across major phases)
- `sync_to_world <WxH>` and `sync_from_world <WxH>` (per-grid timings)
- `sync_to scaling ...` and `sync_from scaling ...` (throughput scaling vs baseline grid)

Interpretation guidance:

- If `sync_to_world` + `sync_from_world` dominates `atomic phase share`, the bottleneck is grid/state copying rather than CAS spread work.
- If `spread` dominates, focus on task granularity, contention, and false-sharing hotspots.
- If `serial core` dominates, optimizing atomic regions alone will not materially improve end-to-end `atomic_tick` latency.
- For scaling lines, values near `1.00x` indicate near-linear throughput retention as grid size grows; lower values indicate cache/memory pressure.

## Expanded Component Baseline (macOS arm64, 2026-03-06)

`test_perf_components` now isolates additional serial-core work:

- `simulation_update_scents()`
- `simulation_resolve_combat()`
- `frontier_telemetry_compute()`

Representative `FEROX_PERF_SCALE=5` snapshot:

- `simulation_update_nutrients`: **67.55 ms**
- `simulation_spread`: **42.12 ms**
- `simulation_update_scents`: **31.36 ms**
- `simulation_resolve_combat`: **35.36 ms**
- `frontier_telemetry_compute`: **110.17 ms**
- `atomic_age` phase: **21.82 ms**
- `atomic_spread` phase: **28.84 ms**
- `server snapshot build`: **44.20 ms**

Interpretation:

- `frontier_telemetry_compute()` is now clearly expensive enough to treat as a first-class optimization target.
- Nutrient diffusion remains the dominant continuous-field update.
- Combat and scent updates matter, but they are secondary to telemetry + nutrient transport in the current local profile.
- Snapshot building is no longer the top bottleneck, which argues for focusing on simulation-core work before transport redesign.

See [docs/PERFORMANCE_RESEARCH.md](/Users/wokuno/Desktop/ferox/docs/PERFORMANCE_RESEARCH.md) for the external research mapping and next experiment list.

## 2026-03-06 Implemented Follow-up

Two suggestions from the research pass were implemented immediately:

1. `frontier_telemetry_compute()` now does one full-grid scan plus a frontier-only sector pass, and caches root-lineage resolution through the existing `colony_by_id` table.
2. `diffuse_scalar_field()` now uses a branch-reduced interior stencil path for normal-sized grids instead of paying four boundary checks on every cell.

Clean validation was taken on `paco` (`Intel Xeon E-2124`) one workload at a time:

- `test_frontier_metrics`: passed
- `test_perf_components` with `FEROX_PERF_SCALE=5`: passed
- `test_performance_eval`: passed `13/13`

Representative `paco` before/after comparison:

- previous broad snapshot:
  - `simulation_tick (serial)`: about `205.70 ms`
  - `atomic_tick (4 threads)`: about `171.31 ms`
  - `atomic/serial`: about `0.87x`
- current broad snapshot:
  - `simulation_tick (serial)`: about `184.09 ms`
  - `atomic_tick (4 threads)`: about `133.13 ms`
  - `atomic/serial`: about `0.79x`

Current `paco` component snapshot (`FEROX_PERF_SCALE=5`):

- `simulation_update_nutrients`: about `85.13 ms`
- `simulation_spread`: about `41.47 ms`
- `simulation_update_scents`: about `60.41 ms`
- `simulation_resolve_combat`: about `37.05 ms`
- `frontier_telemetry_compute`: about `71.73 ms`
- `atomic_age` phase: about `4.74 ms`
- `atomic_spread` phase: about `21.11 ms`
- `server snapshot build`: about `43.62 ms`

Interpretation:

- The telemetry rewrite materially reduced the frontier hotspot on the Xeon.
- The branch-reduced stencil path improved both broad serial throughput and atomic end-to-end throughput by shrinking the serial core inside `atomic_tick()`.
- The next obvious serial-core targets are still scent updates and combat filtering.

Methodology note:

- Do not overlap performance runs or long stress suites on the same machine.
- A local `SimulationLogicTests` stall observed during this pass was reproduced as a scheduling artifact caused by concurrent heavy jobs; `atomic_tick_concurrent_stability` passed when rerun in isolation.

### 2026-03-06 Combat Broadphase Follow-up

Kept:

- `simulation_resolve_combat()` now builds a contested-border frontier during the toxin emission pass and only runs the expensive combat resolution logic on those contested cells.
- `tests/test_perf_components.c` now includes `simulation_resolve_combat_sparse_border_component_eval` so the broadphase has a focused regression/perf harness.

Rejected:

- EPS caching inside `simulation_update_scents()` looked plausible but regressed the isolated scent component on both the M1 Max and the Xeon.
- A per-colony scalar cache for nutrient consumption and scent emission added code complexity and produced mixed-at-best results, with a worse broad atomic result on the Xeon. That experiment was reverted.

Stable kept-state snapshots:

- Local M1 Max, `FEROX_PERF_SCALE=5 test_perf_components`:
  - `simulation_update_nutrients`: `43.03 ms`
  - `simulation_update_scents`: `30.16 ms`
  - `simulation_resolve_combat`: `63.63 ms`
  - `simulation_resolve_combat_sparse_border`: `9.08 ms`
  - `frontier_telemetry_compute`: `62.76 ms`
- `paco` Xeon, `FEROX_PERF_SCALE=5 test_perf_components`:
  - `simulation_update_nutrients`: `86.77 ms`
  - `simulation_update_scents`: `56.37 ms`
  - `simulation_resolve_combat`: `33.44 ms`
  - `simulation_resolve_combat_sparse_border`: `16.84 ms`
  - `frontier_telemetry_compute`: `73.08 ms`
- `paco` Xeon, broad `test_performance_eval`:
  - `simulation_tick (serial)`: `185.66 ms`
  - `atomic_tick (4 threads)`: `133.16 ms`
  - atomic/serial ratio: `0.78x`
  - atomic serial-core share: `107.52 ms`

### 2026-03-06 Telemetry Histogram Rewrite

Kept:

- `frontier_telemetry_compute()` now uses direct-index lineage histograms keyed by resolved root lineage id instead of a linear search through active lineage buckets for each occupied/frontier cell.
- `test_frontier_metrics` stays green after the rewrite.

Measured effect:

- Local M1 Max:
  - `FEROX_PERF_SCALE=5 test_perf_components`: `frontier_telemetry_compute` improved from `62.76 ms` to `27.81 ms`
  - broad `test_performance_eval` seeded telemetry metric moved only slightly, from `90.03 ms` to `88.28 ms`
- `paco` Xeon:
  - `FEROX_PERF_SCALE=5 test_perf_components`: `frontier_telemetry_compute` improved from `73.08 ms` to `41.18 ms`
  - broad `test_performance_eval` seeded telemetry metric moved only slightly, from `112.71 ms` to `111.54 ms`

Interpretation:

- The component-level telemetry kernel clearly benefited.
- The broader seeded telemetry benchmark still spends enough time on world-state characteristics and frontier size that the histogram improvement shows up only modestly there.

### 2026-03-06 Snapshot + No-Biofilm Transport Follow-up

Kept:

- `server_build_protocol_world_snapshot()` now uses a direct `colony_id -> proto_idx` table during the snapshot grid pass instead of searching the protocol colony list for each occupied cell.
- `simulation_update_nutrients()`, toxin transport, and `simulation_update_scents()` now skip EPS attenuation logic entirely when no active colony has non-zero biofilm strength.

Clean validation:

- Local M1 Max:
  - `test_perf_components`: passed `8/8`
  - `test_performance_eval`: passed `13/13`
  - representative `FEROX_PERF_SCALE=5` snapshot:
    - `simulation_update_nutrients`: `27.20 ms`
    - `simulation_update_scents`: `30.84 ms`
    - `simulation_resolve_combat`: `60.66 ms`
    - `simulation_resolve_combat_sparse_border`: `5.19 ms`
    - `frontier_telemetry_compute`: `24.92 ms`
    - `server snapshot build`: `31.28 ms`
  - broad snapshot:
    - `simulation_tick (serial)`: `140.18 ms`
    - `frontier telemetry compute`: `84.05 ms`
    - `broadcast build snapshot`: `3.20 ms`
    - `atomic_tick (4 threads)`: `139.90 ms`
    - atomic/serial ratio: `0.97x`
- `paco` Xeon:
  - `test_perf_components`: passed `8/8`
  - `test_performance_eval`: passed `13/13`
  - representative `FEROX_PERF_SCALE=5` snapshot:
    - `simulation_update_nutrients`: `69.94 ms`
    - `simulation_update_scents`: `56.59 ms`
    - `simulation_resolve_combat`: `34.45 ms`
    - `simulation_resolve_combat_sparse_border`: `11.36 ms`
    - `frontier_telemetry_compute`: `41.10 ms`
    - `server snapshot build`: `40.77 ms`
  - broad snapshot:
    - `simulation_tick (serial)`: `187.84 ms`
    - `frontier telemetry compute`: `111.02 ms`
    - `broadcast build snapshot`: `4.25 ms`
    - `atomic_tick (4 threads)`: `125.62 ms`
    - atomic/serial ratio: `0.73x`

Interpretation:

- The snapshot id-lookup rewrite is a clean keep. It reduces the focused snapshot build cost on both machines and improves the broad broadcast-build stage without changing behavior.
- The no-biofilm fast path is also a keep. The isolated nutrient win is strong on both machines, while the broader serial-tick effect is smaller and mixed on the Xeon.
- The remaining broad bottleneck is still the serial core inside `simulation_tick()`, not the snapshot path.

### Previous baseline (before optimizations)

- `simulation_tick` (serial): 904 ms → **533 ms** (41% faster)
- Protocol throughput: 617 MB/s → **631 MB/s** (2% faster)

## Optimizations Applied

### ✅ O(1) colony lookup
- Added `colony_by_id[]` pointer table to World struct (direct ID→Colony* mapping).
- `world_get_colony()` is now array index lookup instead of linear scan.

### ✅ Pre-allocated atomic region work buffers
- `AtomicRegionWork` array allocated once in `atomic_world_create()` and reused each tick.
- Removed per-task `malloc` in `submit_region_tasks()`.

### ✅ Simulation pass fusion (`simulation_consume_resources`)
- Fused 3 separate full-grid passes (consumption, regeneration, toxin decay) into 1 pass.
- Eliminated redundant `world_get_cell()` calls (direct array access).
- Toxin decay still uses SIMD path via `simd_sub_clamp01_inplace`.

### ✅ Scent diffusion scratch buffer reuse
- Replaced per-tick `calloc` in `simulation_update_scents()` with world-owned scratch buffers.
- Eliminates 2× `calloc` + 2× `free` per tick (~640 KB allocation per tick on 400×200 grid).
- Replaced scalar source copy loop with `memcpy`.

### ✅ EPS-attenuated transport with reusable field buffers
- Nutrient and toxin transport now use world-owned scratch arrays (`scratch_nutrients`, `scratch_toxins`).
- Avoids transient allocations while adding explicit diffusion passes for both fields.
- Tradeoff: extra per-cell arithmetic (`powf` + neighbor flux sums) in exchange for better transport fidelity.

### ✅ Protocol buffer optimization
- RLE serialize starts with smaller initial allocation (size/2 estimate) and grows if needed.
- Removed final shrink-realloc (buffer is immediately consumed).
- World state serialization writes grid directly into message buffer (no intermediate copy).
- Deserialize zero-fill uses `memset` instead of scalar loop.

### ✅ Broadcast/protocol build-path optimization
- `server_build_protocol_world_snapshot()` now folds grid copy + centroid accumulation into one pass.
- Removed per-broadcast heap allocations for centroid accumulation (stack-backed fixed arrays).
- Replaced per-cell colony lookup + division/modulo work with direct colony-id to proto-index lookup and row/column iteration.

### ✅ Snapshot id-lookup rewrite
- `server_build_protocol_world_snapshot()` now builds a dense `colony_id -> proto_idx` lookup table once per snapshot instead of doing compare-heavy searches through the protocol colony list during the grid pass.
- The lookup stays stack-backed for normal colony-id ranges and only falls back to heap allocation for unusually sparse large id spaces.

### ✅ No-biofilm transport fast path
- Nutrient, toxin, and scent transport now skip EPS attenuation logic entirely when no active colony has non-zero biofilm strength.
- This keeps the common no-biofilm case on the plain 4-neighbor diffusion path instead of paying repeated EPS lookup and attenuation work.

## Remaining Bottlenecks

### 1) Atomic tick path still slower than serial
The atomic path has irreducible overhead from barrier synchronization and full-grid sync
(`atomic_world_sync_to_world` + `atomic_world_sync_from_world`). On small grids (400×200),
the parallel regions are too small to amortize thread coordination costs. The atomic path
would benefit from larger grids (1000×1000+) or reduced sync frequency.

### 2) Multiple grid passes remain in simulation
Functions like `simulation_resolve_combat()` (2 passes + toxin diffusion), `simulation_update_scents()` (2 passes + diffusion),
and `simulation_spread()` still do separate full-grid iterations. Further fusion is possible but
adds code complexity.

### 3) No delta protocol for state broadcast
Server sends full `MSG_WORLD_STATE` snapshots each tick. A delta/diff protocol could reduce
network bandwidth by 10-100× for mostly-static grids.

## Recommended Further Optimizations

1. **Larger grid perf mode** — Run atomic path benchmarks on 1000×1000 grids to validate thread scaling.
2. **Tiled transport updates** — Block nutrient/scent stencils so active rows fit cache better.
3. **Dirty frontier / dirty tiles** — Stop rescanning the full grid for telemetry, combat, and eventually snapshots.
4. **Combat pass filtering** — Build a cheap mixed-border/tile broadphase before expensive combat scans.
5. **Protocol delta stream** — Send only changed cells instead of full grid snapshots once dirty metadata exists.
6. **SIMD spread/combat** — Vectorize inner loops of `simulation_spread()` and `simulation_resolve_combat()`.

## Optional External Profiling Tools

If deeper profiling is needed beyond built-in tests:
- **macOS**: Instruments (Time Profiler, Allocations)
- **Linux**: `perf`, `heaptrack`, `valgrind --tool=callgrind`

Built-in performance tests are great for regression tracking; external profilers are better for precise flamegraph hotspot attribution.
