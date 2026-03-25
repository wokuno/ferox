# Lab Note 20260324-171525 broadcast-inline-grid-allocation

## Starting Context

- Run label: `20260324-171525-broadcast-inline-grid-allocation`
- Repository state reviewed with `git status --short` before edits.
- Prior handoff reviewed at `docs/agent-handoffs/latest.md`.
- Prior notebook reviewed at `docs/lab-notes/20260324-165209-snapshot-colony-id-lookup.md`.
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
  - `src/shared/protocol.c`
  - `src/shared/protocol.h`
  - `tests/test_performance_eval.c`
  - `tests/test_protocol_edge.c`
  - `tests/test_phase2.c`

## Research Topics

1. Broader broadcast benchmark target.
   - Takeaway: `server_broadcast_path_breakdown_eval` is the right corroboration lane because it isolates snapshot build, build+serialize, and zero-client broadcast costs in one test.
   - Sources: `tests/test_performance_eval.c`

2. Current broad baseline status.
   - Takeaway: the second run already had a 3-run baseline with medians `9.41 ms` build, `20.62 ms` build+serialize, `20.38 ms` end-to-end.
   - Sources: prior run notes in conversation state

3. Inline-grid path activation.
   - Takeaway: the default `400x200` benchmark world is always under `MAX_INLINE_GRID_SIZE`, so full inline snapshot allocation happens every measured iteration.
   - Sources: `tests/test_performance_eval.c`, `src/shared/protocol.h`

4. Broadcast path composition.
   - Takeaway: `server_broadcast_world_state()` builds a `ProtoWorld`, serializes it, then skips chunk-buffer work for inline grids.
   - Sources: `src/server/server.c`

5. Snapshot allocation path.
   - Takeaway: `server_build_protocol_world_snapshot()` calls `proto_world_alloc_grid()` before copying every cell from `world->cells` into `proto_world->grid`.
   - Sources: `src/server/server.c`, `src/shared/protocol.c`

6. Zero-fill behavior in helper.
   - Takeaway: `proto_world_alloc_grid()` always `memset`s the whole grid to zero immediately after `malloc`.
   - Sources: `src/shared/protocol.c`

7. Grid overwrite completeness.
   - Takeaway: the snapshot build loop writes every `proto_world->grid[idx]` unconditionally whenever inline grid is enabled, so the initial zero-fill is redundant on that path.
   - Sources: `src/server/server.c`

8. Safety of direct allocation.
   - Takeaway: direct `malloc` is safe if `grid`, `grid_size`, and `has_grid` are initialized consistently and existing failure cleanup is preserved.
   - Sources: `src/shared/protocol.c`, `src/server/server.c`

9. ProtoWorld lifecycle contract.
   - Takeaway: `proto_world_init()` zeros the struct and `proto_world_free()` only frees `grid`, so bypassing `proto_world_alloc_grid()` does not violate ownership rules.
   - Sources: `src/shared/protocol.c`

10. Serialization dependency on `has_grid`.
    - Takeaway: `protocol_serialize_world_state()` only serializes grid data when `has_grid`, `grid`, and `grid_size` are all set correctly.
    - Sources: `src/shared/protocol.c`

11. Existing grid roundtrip coverage.
    - Takeaway: protocol edge tests already cover no-grid framing and raw/RLE grid codecs, but did not have a direct populated world-state grid roundtrip test.
    - Sources: `tests/test_protocol_edge.c`

12. Correctness regression risk.
    - Takeaway: the most likely bug from direct allocation is forgetting to set grid metadata, not cell corruption during the copy loop.
    - Sources: `src/server/server.c`, `src/shared/protocol.c`

13. Allocation cost guidance.
    - Takeaway: redundant initialization of large transient buffers on hot paths is a classic measurable micro-optimization target when the buffer will be fully overwritten before observation.
    - Sources: <https://en.cppreference.com/w/c/string/byte/memset>, <https://man7.org/linux/man-pages/man3/malloc.3.html>

14. Inline grid size on current benchmark.
    - Takeaway: `400x200` means `80,000` cells, so the removed zero-fill avoids touching `160 KiB` per snapshot before the copy loop starts.
    - Sources: `tests/test_performance_eval.c`, `src/shared/protocol.h`

15. Alternative transport target: serializer buffer reuse.
    - Takeaway: `protocol_serialize_world_state()` also allocates fresh encoded buffers each iteration, but that change would cut across a broader API surface and is less narrow for this cycle.
    - Sources: `src/shared/protocol.c`

16. Alternative transport target: chunked delta prep.
    - Takeaway: chunk buffer generation is inactive in the default benchmark because inline grid snapshots are used.
    - Sources: `src/server/server.c`, `src/shared/protocol.h`

17. Alternative target: client mutex cost.
    - Takeaway: zero-client benchmark means the client list is empty, so mutex/list work is present but not dominant; build and serialize still dominate.
    - Sources: `src/server/server.c`, `tests/test_performance_eval.c`

18. Prior focused snapshot win context.
    - Takeaway: the previous run already reduced snapshot build indirection, so a second narrow change should avoid reworking that same logic heavily and instead trim adjacent overhead.
    - Sources: `docs/PERFORMANCE.md`, `docs/agent-handoffs/latest.md`

19. Existing dirty worktree constraint.
    - Takeaway: unrelated user changes remain in docs/scripts/root files and must be left untouched.
    - Sources: `git status --short`

20. Test relevance.
    - Takeaway: `test_protocol_edge` is the right correctness net for world-state wire behavior, and `test_phase2` protects the sparse-id snapshot path changed in the previous run.
    - Sources: `docs/TESTING.md`, `tests/test_protocol_edge.c`, `tests/test_phase2.c`

21. Benchmark stability expectation.
    - Takeaway: the broad benchmark is noisier than the focused component test, so repeated medians and all-runs-better checks matter before keeping a micro-optimization.
    - Sources: `docs/PERF_RUNBOOK.md`, `tests/test_performance_eval.c`

22. Documentation requirement.
    - Takeaway: autonomous-cycle rules require a notebook, updated performance docs, and a fresh handoff even when the experiment is tiny.
    - Sources: user run contract, `docs/DEVELOPMENT_CYCLE.md`

## Chosen Hypothesis

If `server_build_protocol_world_snapshot()` allocates the inline grid directly instead of calling `proto_world_alloc_grid()` first, repeated `test_performance_eval` medians for `broadcast build snapshot` and `broadcast build+serialize` will improve on the default profile because the current path needlessly zero-fills a grid buffer that the snapshot loop fully overwrites.

## Experiment

- Replaced the inline-grid allocation inside `server_build_protocol_world_snapshot()` with direct `malloc` plus explicit `grid_size`/`has_grid` initialization.
- Left the existing snapshot scan and cleanup behavior unchanged.
- Added `world_state_with_grid_roundtrip_preserves_cells` to `tests/test_protocol_edge.c` so populated world-state grid payloads still round-trip through serialize/deserialize.

## Tests Run

1. Build command:

```bash
cmake --build build -j4 --target test_protocol_edge test_phase2 test_performance_eval
```

2. Correctness tests:

```bash
./build/tests/test_protocol_edge
./build/tests/test_phase2
```

3. Benchmark gate and measurement command:

```bash
python3 - <<'PY'
import subprocess, re, statistics, os
pattern_build = re.compile(r'\[perf\]\s+broadcast build snapshot\s+([0-9.]+) ms')
pattern_bs = re.compile(r'\[perf\]\s+broadcast build\+serialize\s+([0-9.]+) ms')
pattern_end = re.compile(r'\[perf\]\s+broadcast end-to-end \(0 clients\)\s+([0-9.]+) ms')
env = os.environ.copy()
env['FEROX_PERF_SCALE'] = '5'
runs = []
for i in range(3):
    out = subprocess.check_output(['./build/tests/test_performance_eval'], env=env, text=True)
    build = float(pattern_build.search(out).group(1))
    bs = float(pattern_bs.search(out).group(1))
    end = float(pattern_end.search(out).group(1))
    runs.append((build, bs, end))
    print(build, bs, end)
print(statistics.median(v[0] for v in runs))
print(statistics.median(v[1] for v in runs))
print(statistics.median(v[2] for v in runs))
PY
```

Result:

- `test_protocol_edge`: passed
- `test_phase2`: passed
- `test_performance_eval`: passed in each measured run

## Benchmark Results

Baseline before code change (`FEROX_PERF_SCALE=5`, from second-run startup):

- run 1: build `9.56 ms`, build+serialize `20.62 ms`, end `20.38 ms`
- run 2: build `9.29 ms`, build+serialize `20.47 ms`, end `20.22 ms`
- run 3: build `9.41 ms`, build+serialize `21.27 ms`, end `21.41 ms`
- medians: build `9.41 ms`, build+serialize `20.62 ms`, end `20.38 ms`

Candidate after code change (`FEROX_PERF_SCALE=5`):

- run 1: build `7.01 ms`, build+serialize `17.01 ms`, end `17.22 ms`
- run 2: build `6.98 ms`, build+serialize `17.05 ms`, end `17.17 ms`
- run 3: build `7.26 ms`, build+serialize `18.02 ms`, end `17.59 ms`
- medians: build `7.01 ms`, build+serialize `17.05 ms`, end `17.22 ms`

Delta:

- `broadcast build snapshot`: `2.40 ms` faster median, about `25.5%` improvement
- `broadcast build+serialize`: `3.57 ms` faster median, about `17.3%` improvement
- `broadcast end-to-end (0 clients)`: `3.16 ms` faster median, about `15.5%` improvement

## Interpretation

- The hypothesis was supported.
- All three candidate runs beat all three baseline runs across all three broadcast metrics, so the result is stronger than a small median-only movement.
- Removing the redundant inline-grid zero-fill appears to compound cleanly with the earlier colony-id lookup improvement: snapshot build fell again and the broader build+serialize lane moved with it.
- After this change, serializer cost remains the larger share of the transport path, making `protocol_serialize_world_state()` the most natural next narrow target.

## Docs Updated

- `docs/PERFORMANCE.md`
- `docs/PERFORMANCE_RESEARCH.md`
- `docs/agent-handoffs/latest.md`
- `docs/lab-notes/20260324-171525-broadcast-inline-grid-allocation.md`

## Next Recommended Experiment

Target `protocol_serialize_world_state()` in the inline-grid path, likely by reducing temporary allocation/copy overhead or avoiding unnecessary work before `protocol_serialize_grid_rle()` decides between raw and RLE modes.
