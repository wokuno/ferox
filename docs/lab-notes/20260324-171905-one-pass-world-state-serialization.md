# Lab Note 20260324-171905 one-pass-world-state-serialization

## Starting Context

- Run label: `20260324-171905-one-pass-world-state-serialization`
- Repository state reviewed with `git status --short --branch` before edits.
- Prior handoff reviewed at `docs/agent-handoffs/latest.md`.
- Prior notebooks reviewed:
  - `docs/lab-notes/20260324-165209-snapshot-colony-id-lookup.md`
  - `docs/lab-notes/20260324-171525-broadcast-inline-grid-allocation.md`
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
  - `src/shared/protocol.c`
  - `src/shared/protocol.h`
  - `tests/test_perf_unit_protocol.c`
  - `tests/test_performance_eval.c`
  - `tests/test_protocol_edge.c`
  - `tests/test_phase4.c`

## Prior Handoff Decision

- Accepted in spirit, but narrowed after fresh research.
- The prior handoff said to target `protocol_serialize_world_state()` in the inline-grid path.
- Fresh research suggested the narrowest reversible slice was not API-level buffer reuse, but eliminating the temporary encoded-grid allocation and copy inside the existing serializer.

## Research Topics

1. Current transport bottleneck after the last run.
   - Takeaway: broad transport cost is now dominated more by serialization than snapshot build.
   - Sources: `docs/PERFORMANCE.md`, `tests/test_performance_eval.c`

2. Fresh broad baseline on current HEAD.
   - Takeaway: current medians are `13.75 ms` protocol serialize only, `7.45 ms` build snapshot, `17.66 ms` build+serialize, `17.90 ms` end-to-end.
   - Sources: repeated `FEROX_PERF_SCALE=5 ./build/tests/test_performance_eval`

3. Focused protocol unit baseline shape.
   - Takeaway: `test_perf_unit_protocol` shows noisy grids already serialize at roughly raw-size ratio, so the main cost is CPU work rather than payload bloat.
   - Sources: `./build/tests/test_perf_unit_protocol`

4. `protocol_serialize_world_state()` allocation pattern.
   - Takeaway: the function first allocates a temporary `grid_buffer`, then allocates the final message buffer, then copies the grid payload again.
   - Sources: `src/shared/protocol.c`

5. Temporary grid payload lifetime.
   - Takeaway: `grid_buffer` exists only long enough to compute `grid_len` and then `memcpy` into the final output.
   - Sources: `src/shared/protocol.c`

6. Grid payload size upper bound.
   - Takeaway: the existing raw mode already proves the useful upper bound for any encoded grid is `5 + size * 2` bytes, not the older worst-case RLE allocation size.
   - Sources: `src/shared/protocol.c`

7. RLE fallback behavior.
   - Takeaway: when runs are too short, the encoder switches to raw payload anyway, which makes the temporary buffer especially redundant on noisy grids.
   - Sources: `src/shared/protocol.c`, `tests/test_perf_unit_protocol.c`

8. Inline-grid broadcast profile data shape.
   - Takeaway: encoded world-state size stays around `4.82 KiB` on the default broadcast benchmark, so redundant copies matter more than bandwidth.
   - Sources: `tests/test_performance_eval.c`, `docs/PERFORMANCE.md`

9. Serializer/deserializer balance.
   - Takeaway: `protocol_world_path_breakdown_eval` reports serialize and deserialize costs are close, so a serializer-only win is likely measurable but not dramatic.
   - Sources: `tests/test_performance_eval.c`

10. Wire-format safety requirements.
    - Takeaway: any optimization must preserve the exact prefix layout and raw/RLE mode bytes used by existing protocol tests.
    - Sources: `tests/test_protocol_edge.c`, `tests/test_phase4.c`

11. Existing no-grid coverage.
    - Takeaway: no-grid wire framing is already covered by `world_state_without_grid_uses_fixed_prefix`.
    - Sources: `tests/test_protocol_edge.c`

12. Existing populated-grid coverage gap.
    - Takeaway: there was a small populated-grid roundtrip test, but no larger noisy-grid world-state roundtrip covering the raw-mode-heavy path in `protocol_serialize_world_state()`.
    - Sources: `tests/test_protocol_edge.c`

13. `protocol_serialize_grid_rle()` duplication.
    - Takeaway: the raw-mode writing logic existed twice inside the function and was also conceptually repeated by the world-state serializer's outer copy.
    - Sources: `src/shared/protocol.c`

14. `malloc()` cost guidance.
    - Takeaway: transient allocations are uninitialized and thread-safe but still introduce avoidable work and possible arena contention on hot paths.
    - Sources: <https://man7.org/linux/man-pages/man3/malloc.3.html>

15. `memcpy()` cost guidance.
    - Takeaway: `memcpy` is fast, but still explicit memory traffic, so removing a whole encoded-grid copy is a valid micro-optimization target.
    - Sources: <https://en.cppreference.com/w/c/string/byte/memcpy>

16. `memset()` relevance.
    - Takeaway: the previous run already removed one redundant initialization pass in snapshot building; this run can follow the same principle for encoded-grid staging.
    - Sources: <https://en.cppreference.com/w/c/string/byte/memset>, `docs/lab-notes/20260324-171525-broadcast-inline-grid-allocation.md`

17. Default benchmark mode selection.
    - Takeaway: broadcast path uses inline full snapshots, not chunked delta transport, so `protocol_serialize_world_state()` is the right transport slice here.
    - Sources: `src/server/server.c`, `src/shared/protocol.h`, `tests/test_performance_eval.c`

18. API compatibility risk.
    - Takeaway: changing `protocol_serialize_world_state()` internals without changing its signature is low-risk and easy to revert.
    - Sources: `src/shared/protocol.h`, `src/shared/protocol.c`

19. Candidate alternative: caller-owned output buffer reuse.
    - Takeaway: likely promising, but it would broaden API surface and touch more call sites than is ideal for one experiment cycle.
    - Sources: `src/shared/protocol.c`, `tests/test_performance_eval.c`, `src/server/server.c`

20. Candidate alternative: raw-mode bulk conversion shortcut.
    - Takeaway: likely useful later, but endian-safe bulk conversion is less narrow than first removing the obvious temporary buffer.
    - Sources: `src/shared/protocol.c`

21. Candidate alternative: deserializer allocation cleanup.
    - Takeaway: deserialization is not in the server broadcast path, so it is less aligned with the current transport-facing broad benchmark.
    - Sources: `tests/test_performance_eval.c`, `src/shared/protocol.c`

22. Candidate alternative: protocol colony serialization loop.
    - Takeaway: colony serialization is only `72` colonies in the perf eval lane and likely smaller than grid-path work, so it is not the best first slice.
    - Sources: `tests/test_performance_eval.c`, `src/shared/protocol.h`

23. Dirty worktree constraint.
    - Takeaway: unrelated user edits remain in root/docs/scripts and must stay untouched.
    - Sources: `git status --short --branch`

24. Validation ladder for this slice.
    - Takeaway: `test_protocol_edge` and `test_perf_unit_protocol` are the right pre-benchmark gates, with `test_performance_eval` as the 3-run comparison lane.
    - Sources: `docs/TESTING.md`, `docs/PERF_RUNBOOK.md`

## Candidate Experiment Ideas

1. Write the encoded grid directly into the final world-state output buffer.
   - Pros: narrow, reversible, measurable, no API changes.
   - Cons: needs careful length bookkeeping.

2. Add caller-provided scratch/output buffer reuse for `protocol_serialize_world_state()`.
   - Pros: potentially larger allocation win.
   - Cons: broader API churn and more call-site edits.

3. Add a raw-mode fast path that writes the world grid with less per-cell overhead.
   - Pros: directly targets noisy grids.
   - Cons: endian-safe implementation is more invasive.

4. Optimize deserialization allocation/copying.
   - Pros: should help `protocol deserialize only`.
   - Cons: less relevant to current server broadcast lane.

## Triage Reasoning

- Idea 1 is the best fit for one cycle because it has clear redundant work, stays inside one file plus tests, and maps directly onto the active broadcast-path bottleneck.
- Idea 2 is attractive but too broad for a single reversible experiment.
- Idea 3 is plausible, but it should come after removing the more obvious double-buffering first.
- Idea 4 is worth keeping in reserve, but it does not improve the server-side zero-client broadcast lane as directly.

## Chosen Hypothesis

If `protocol_serialize_world_state()` serializes the encoded grid directly into the final output buffer instead of allocating a temporary grid buffer and copying it, repeated `FEROX_PERF_SCALE=5` medians for `protocol serialize only` and `broadcast build+serialize` will improve on the default profile without changing wire correctness.

## Experiment

- Added a small shared helper that can encode grid payloads into caller-provided storage and reuse the same raw/RLE decision logic.
- Changed `protocol_serialize_world_state()` to allocate one final output buffer sized for the header, colonies, and worst useful grid payload, then write grid data directly into that final buffer.
- Kept the public serializer signature unchanged.
- Added `world_state_with_noisy_grid_roundtrip_preserves_cells` to `tests/test_protocol_edge.c` so raw-mode-heavy world-state serialization still round-trips correctly.

## Tests Run

1. Build command:

```bash
cmake --build build -j4 --target test_protocol_edge test_perf_unit_protocol test_performance_eval
```

2. Correctness/perf gates before benchmarking:

```bash
./build/tests/test_protocol_edge
./build/tests/test_perf_unit_protocol
```

3. Benchmark command:

```bash
python3 - <<'PY'
import subprocess, re, statistics, os
patterns = {
    'proto_ser': re.compile(r'\[perf\]\s+protocol serialize only\s+([0-9.]+) ms'),
    'proto_de': re.compile(r'\[perf\]\s+protocol deserialize only\s+([0-9.]+) ms'),
    'build': re.compile(r'\[perf\]\s+broadcast build snapshot\s+([0-9.]+) ms'),
    'build_ser': re.compile(r'\[perf\]\s+broadcast build\+serialize\s+([0-9.]+) ms'),
    'end': re.compile(r'\[perf\]\s+broadcast end-to-end \(0 clients\)\s+([0-9.]+) ms'),
}
env = os.environ.copy()
env['FEROX_PERF_SCALE'] = '5'
runs = []
for i in range(3):
    out = subprocess.check_output(['./build/tests/test_performance_eval'], env=env, text=True)
    vals = {k: float(p.search(out).group(1)) for k, p in patterns.items()}
    runs.append(vals)
    print(vals)
for key in ['proto_ser','proto_de','build','build_ser','end']:
    print(key, statistics.median(v[key] for v in runs))
PY
```

Result:

- `test_protocol_edge`: passed
- `test_perf_unit_protocol`: passed
- `test_performance_eval`: passed in each measured run

## Benchmark Results

Baseline before code change (`FEROX_PERF_SCALE=5`):

- run 1: protocol serialize `13.27 ms`, protocol deserialize `14.22 ms`, build `7.10 ms`, build+serialize `16.99 ms`, end `17.16 ms`
- run 2: protocol serialize `13.75 ms`, protocol deserialize `14.15 ms`, build `7.45 ms`, build+serialize `17.66 ms`, end `17.90 ms`
- run 3: protocol serialize `13.85 ms`, protocol deserialize `14.28 ms`, build `7.55 ms`, build+serialize `18.05 ms`, end `18.04 ms`
- medians: protocol serialize `13.75 ms`, protocol deserialize `14.22 ms`, build `7.45 ms`, build+serialize `17.66 ms`, end `17.90 ms`

Candidate after code change (`FEROX_PERF_SCALE=5`):

- run 1: protocol serialize `14.12 ms`, protocol deserialize `14.86 ms`, build `7.05 ms`, build+serialize `16.59 ms`, end `16.50 ms`
- run 2: protocol serialize `12.91 ms`, protocol deserialize `14.09 ms`, build `7.01 ms`, build+serialize `16.48 ms`, end `16.51 ms`
- run 3: protocol serialize `13.25 ms`, protocol deserialize `14.13 ms`, build `7.50 ms`, build+serialize `17.32 ms`, end `17.20 ms`
- medians: protocol serialize `13.25 ms`, protocol deserialize `14.13 ms`, build `7.05 ms`, build+serialize `16.59 ms`, end `16.51 ms`

Delta:

- `protocol serialize only`: `0.50 ms` faster median, about `3.6%` improvement
- `protocol deserialize only`: `0.09 ms` faster median, effectively flat but slightly better
- `broadcast build snapshot`: `0.40 ms` faster median, likely secondary/noise-adjacent
- `broadcast build+serialize`: `1.07 ms` faster median, about `6.1%` improvement
- `broadcast end-to-end (0 clients)`: `1.39 ms` faster median, about `7.8%` improvement

## Interpretation

- The hypothesis was supported.
- The win is smaller and noisier than the previous two transport passes, but the median moved in the right direction for every tracked lane and the broad transport metrics improved together.
- The kept value here is removing a whole allocation/copy stage without changing the API or wire format.
- The next narrow transport slice should probably target raw-grid encode/decode per-cell write/read overhead rather than broad buffer-lifetime changes.

## Docs Updated

- `docs/PERFORMANCE.md`
- `docs/PERFORMANCE_RESEARCH.md`
- `docs/agent-handoffs/latest.md`
- `docs/lab-notes/20260324-171905-one-pass-world-state-serialization.md`

## Next Recommended Experiment

Target the raw-grid path inside `protocol_serialize_grid_rle()` / `protocol_deserialize_grid_rle()` to reduce per-cell byte-order write/read overhead now that the extra staging buffer is gone.
