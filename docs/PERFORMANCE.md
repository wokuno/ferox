# Ferox Performance Evaluation

This document captures how to run performance measurements, current baseline data, and bottlenecks found in the current implementation.

## How to Run Performance Tests

```bash
# Fast run
./scripts/test.sh perf

# Heavier run (recommended for comparisons)
FEROX_PERF_SCALE=5 ./scripts/test.sh perf
```

`FEROX_PERF_SCALE` increases loop counts in `test_performance_eval.c` so timings are more stable.

## Current Baseline (macOS arm64, 3 runs, `FEROX_PERF_SCALE=5`)

Average values from repeated runs:

- SIMD toxin decay ratio (`optimized/scalar`): **1.53x** (scalar faster on NEON; no regression)
- Name + color generation: **39.6 ms** for 250k iterations
- World create/init/destroy: **32.8 ms** for 900 iterations
- Protocol serialize+deserialize: **24.0 ms** for 600 iterations (**630.6 MB/s**)
- `simulation_tick` (serial): **533 ms** for 175 ticks (~3.0 ms/tick)
- `atomic_tick` (4 threads): **1299 ms** for 150 ticks (~8.7 ms/tick)
- Atomic vs serial ratio: **2.19x** (atomic path still slower; see notes)
- Threadpool submit+execute: **37.6 ms** for 250k tasks

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

### ✅ Protocol buffer optimization
- RLE serialize starts with smaller initial allocation (size/2 estimate) and grows if needed.
- Removed final shrink-realloc (buffer is immediately consumed).
- World state serialization writes grid directly into message buffer (no intermediate copy).
- Deserialize zero-fill uses `memset` instead of scalar loop.

## Remaining Bottlenecks

### 1) Atomic tick path still slower than serial
The atomic path has irreducible overhead from barrier synchronization and full-grid sync
(`atomic_world_sync_to_world` + `atomic_world_sync_from_world`). On small grids (400×200),
the parallel regions are too small to amortize thread coordination costs. The atomic path
would benefit from larger grids (1000×1000+) or reduced sync frequency.

### 2) Multiple grid passes remain in simulation
Functions like `simulation_resolve_combat()` (2 passes), `simulation_update_scents()` (2 passes + diffusion),
and `simulation_spread()` still do separate full-grid iterations. Further fusion is possible but
adds code complexity.

### 3) No delta protocol for state broadcast
Server sends full `MSG_WORLD_STATE` snapshots each tick. A delta/diff protocol could reduce
network bandwidth by 10-100× for mostly-static grids.

## Recommended Further Optimizations

1. **Larger grid perf mode** — Run atomic path benchmarks on 1000×1000 grids to validate thread scaling.
2. **Combat pass fusion** — Combine toxin emission + combat resolution into single pass.
3. **Protocol delta stream** — Send only changed cells instead of full grid snapshots.
4. **SIMD spread/combat** — Vectorize inner loops of `simulation_spread()` and `simulation_resolve_combat()`.

## Optional External Profiling Tools

If deeper profiling is needed beyond built-in tests:
- **macOS**: Instruments (Time Profiler, Allocations)
- **Linux**: `perf`, `heaptrack`, `valgrind --tool=callgrind`

Built-in performance tests are great for regression tracking; external profilers are better for precise flamegraph hotspot attribution.

