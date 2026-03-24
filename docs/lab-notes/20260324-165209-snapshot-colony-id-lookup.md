# Lab Note 20260324-165209 snapshot-colony-id-lookup

## Starting Context

- Run label: `20260324-165209-snapshot-colony-id-lookup`
- Repository state reviewed with `git status --short --branch` before edits.
- No prior handoff file was present at `docs/agent-handoffs/latest.md`.
- No prior run notebooks were present under `docs/lab-notes/`.
- Required docs reviewed:
  - `README.md`
  - `docs/DEVELOPMENT_CYCLE.md`
  - `docs/TESTING.md`
  - `docs/PERF_RUNBOOK.md`
  - `docs/PERFORMANCE.md`
  - `docs/PERFORMANCE_RESEARCH.md`
  - `docs/SCIENCE_BENCHMARKS.md`
  - `docs/CODEBASE_REVIEW_LOG.md`
- Subsystem code/tests reviewed:
  - `src/server/server.c`
  - `src/server/frontier_metrics.c`
  - `src/server/simulation.c`
  - `src/server/world.c`
  - `tests/test_perf_components.c`
  - `tests/test_phase2.c`
  - `tests/test_frontier_metrics.c`
  - `tests/test_combat_system.c`
  - `tests/test_performance_eval.c`

## Research Topics

1. Snapshot build hotspot location.
   - Takeaway: `server_build_protocol_world_snapshot()` is still a focused measurable path in `tests/test_perf_components.c` and `tests/test_performance_eval.c`.
   - Sources: `src/server/server.c`, `tests/test_perf_components.c`, `tests/test_performance_eval.c`

2. Existing snapshot optimization history.
   - Takeaway: the last kept snapshot win was a direct `colony_id -> proto_idx` lookup during the grid pass, so the next safe slice is reducing remaining indirection.
   - Sources: `docs/PERFORMANCE.md`, `docs/PERFORMANCE_RESEARCH.md`

3. Current snapshot lookup shape.
   - Takeaway: snapshot code still maps `colony_id -> world_index -> proto_index`, which adds an extra dependent array lookup per occupied cell.
   - Sources: `src/server/server.c`

4. World lookup infrastructure.
   - Takeaway: `World` already maintains `colony_index_map` sized by colony id, so a direct proto-index table can reuse the same sparse-id capacity safely.
   - Sources: `src/shared/types.h`, `src/server/world.c`

5. Colony id sparsity risk.
   - Takeaway: colony ids monotonically increase and removed colonies leave holes, so lookup structures must tolerate high sparse ids.
   - Sources: `src/server/world.c`

6. Inline snapshot bounds.
   - Takeaway: default profile (`400x200`) stays inline because `MAX_INLINE_GRID_SIZE` is `800 * 400`, so every benchmark run exercises the full grid fill path.
   - Sources: `src/shared/protocol.h`

7. Protocol colony bound.
   - Takeaway: only `MAX_COLONIES` active colonies are serialized, so a `uint16_t` proto index table is sufficient and smaller than `uint32_t`.
   - Sources: `src/shared/protocol.h`

8. Default profile colony counts.
   - Takeaway: benchmark scenarios use `50` colonies, so a small stack-backed table can cover common cases with no heap allocation.
   - Sources: `README.md`, `tests/test_perf_components.c`, `docs/PERFORMANCE.md`

9. Sparse high-id correctness gap.
   - Takeaway: existing tests did not cover a world where only a few active colonies remain but ids have grown past a small stack threshold.
   - Sources: `tests/test_phase2.c`, `tests/test_protocol_edge.c`

10. Snapshot build benchmark methodology.
    - Takeaway: component benchmark runs `server_build_protocol_world_snapshot()` in isolation and is the right narrow benchmark for this experiment.
    - Sources: `tests/test_perf_components.c`, `docs/PERF_RUNBOOK.md`

11. Broad broadcast benchmark methodology.
    - Takeaway: `test_performance_eval` includes broader `broadcast build snapshot` timing, but the component lane is less noisy for a single-slice snapshot experiment.
    - Sources: `tests/test_performance_eval.c`, `docs/TESTING.md`

12. Memory allocation overhead guidance.
    - Takeaway: allocator docs reinforce that avoiding repeated heap allocation on hot repeated paths is a reasonable micro-optimization target.
    - Sources: <https://man7.org/linux/man-pages/man3/malloc.3.html>, <https://en.cppreference.com/w/c/memory/calloc>

13. Memset cost/use guidance.
    - Takeaway: `memset` on a compact integer table is straightforward and already used pervasively in this codebase for setup paths.
    - Sources: <https://en.cppreference.com/w/c/string/byte/memset>

14. Row-major locality reminder.
    - Takeaway: the grid pass is row-major already, so reducing pointer chasing rather than loop order is the better narrow change here.
    - Sources: <https://rrze-hpc.github.io/layer-condition/>, <https://en.wikipedia.org/wiki/Row-_and_column-major_order>

15. Frontier telemetry branch as alternative target.
    - Takeaway: telemetry had recent wins already, and current docs rank it below fresh snapshot-specific cleanup for a narrow reversible pass.
    - Sources: `docs/PERFORMANCE.md`, `docs/PERFORMANCE_RESEARCH.md`, `src/server/frontier_metrics.c`

16. Combat sparse-border branch as alternative target.
    - Takeaway: combat already has a focused sparse-border benchmark, but snapshot build offers a cleaner deterministic data-path experiment for this run.
    - Sources: `tests/test_perf_components.c`, `src/server/simulation.c`, `tests/test_combat_system.c`

17. Scent update branch as alternative target.
    - Takeaway: scent work remains expensive, but earlier caching experiments regressed, so it is not the best one-cycle experiment today.
    - Sources: `docs/PERFORMANCE.md`, `src/server/simulation.c`

18. Handoff/automation expectations.
    - Takeaway: autonomous run rules require one falsifiable hypothesis, repeated-run medians, docs updates, a notebook, and a handoff in the same cycle.
    - Sources: user run contract, `docs/DEVELOPMENT_CYCLE.md`

19. Test matrix relevance.
    - Takeaway: `Phase2Tests` is sufficient to cover the new sparse-id snapshot correctness case, and `PerformanceComponentTests` covers the focused perf lane.
    - Sources: `docs/TESTING.md`, `tests/CMakeLists.txt`

20. Existing dirty worktree constraint.
    - Takeaway: there are unrelated user changes in docs and scripts, so only experiment-specific files should be edited or committed.
    - Sources: `git status --short --branch`

21. Potential failure mode with direct table by id.
    - Takeaway: using `world->colony_index_capacity` rather than `world->colony_count` avoids out-of-bounds on sparse high ids.
    - Sources: `src/shared/types.h`, `src/server/world.c`

22. Potential failure mode with stack-only table.
    - Takeaway: a stack table alone would be unsafe for large sparse id spaces, so the implementation needs stack-or-heap fallback.
    - Sources: `src/server/world.c`, `src/server/server.c`

## Chosen Hypothesis

If `server_build_protocol_world_snapshot()` builds a direct `colony_id -> proto_index` table instead of mapping through `world_index`, the repeated `server snapshot build` median in `test_perf_components` will improve measurably on the default profile without changing snapshot correctness for sparse high colony ids.

## Experiment

- Replaced the per-snapshot `world_index -> proto_index` table with a direct `colony_id -> proto_index` table in `src/server/server.c`.
- Used a stack-backed `uint16_t` table for common small capacities and a heap fallback for larger sparse-id cases.
- Kept the grid pass logic identical except for collapsing the extra `world->colony_index_map[colony_id]` lookup.
- Added `server_build_protocol_world_snapshot_handles_sparse_high_colony_ids` to `tests/test_phase2.c` to cover sparse-id correctness when active colonies remain at ids above the stack threshold.

## Tests Run

1. Build command:

```bash
cmake --build build -j4 --target test_phase2 test_perf_components
```

2. Correctness test:

```bash
./build/tests/test_phase2
```

3. Focused perf validation before benchmarking:

```bash
FEROX_PERF_SCALE=5 ./build/tests/test_perf_components
```

Result:

- `test_phase2`: passed
- `test_perf_components` with `FEROX_PERF_SCALE=5`: passed

## Benchmark Commands

Baseline before code change:

```bash
python3 - <<'PY'
import subprocess, re, statistics
vals=[]
for i in range(3):
    out = subprocess.check_output(['./build/tests/test_perf_components'], env={'FEROX_PERF_SCALE':'5'}, text=True)
    vals.append(float(re.search(r'\[perf\]\s+server snapshot build\s+([0-9.]+) ms', out).group(1)))
    print(vals[-1])
print(statistics.median(vals))
PY
```

Candidate after code change:

```bash
python3 - <<'PY'
import subprocess, re, statistics
vals=[]
for i in range(3):
    out = subprocess.check_output(['./build/tests/test_perf_components'], env={'FEROX_PERF_SCALE':'5'}, text=True)
    vals.append(float(re.search(r'\[perf\]\s+server snapshot build\s+([0-9.]+) ms', out).group(1)))
    print(vals[-1])
print(statistics.median(vals))
PY
```

## Benchmark Results

Baseline `server snapshot build` (`FEROX_PERF_SCALE=5`):

- run 1: `18.75 ms`
- run 2: `18.90 ms`
- run 3: `18.76 ms`
- median: `18.76 ms`

Candidate `server snapshot build` (`FEROX_PERF_SCALE=5`):

- run 1: `15.38 ms`
- run 2: `15.85 ms`
- run 3: `15.61 ms`
- median: `15.61 ms`

Delta:

- median improvement: `3.15 ms`
- relative improvement: about `16.8%` faster

## Interpretation

- The hypothesis was supported.
- The three candidate runs were consistently below all three baseline runs, so this is not just a median-only fluke.
- The win comes from removing one dependent mapping step from each occupied-cell centroid accumulation in the snapshot grid pass.
- Correctness risk stayed low because the new lookup still respects sparse high ids and protocol colony caps.

## Docs Updated

- `docs/PERFORMANCE.md`
- `docs/PERFORMANCE_RESEARCH.md`
- `docs/agent-handoffs/latest.md`
- `docs/lab-notes/20260324-165209-snapshot-colony-id-lookup.md`

## Next Recommended Experiment

Use the same stack-or-heap direct lookup pattern on the broader broadcast build path metrics and confirm whether `broadcast build snapshot` shows a matching repeated-run gain in `test_performance_eval`.
