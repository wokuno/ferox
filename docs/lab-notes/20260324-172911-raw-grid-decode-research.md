# Lab Note 20260324-172911 raw-grid-decode-research

## Starting Context

- Run label: `20260324-172911-raw-grid-decode-research`
- Repository state reviewed with `git status --short --branch` before edits.
- Prior handoff reviewed at `docs/agent-handoffs/latest.md`.
- Prior notebooks reviewed:
  - `docs/lab-notes/20260324-165209-snapshot-colony-id-lookup.md`
  - `docs/lab-notes/20260324-171525-broadcast-inline-grid-allocation.md`
  - `docs/lab-notes/20260324-171905-one-pass-world-state-serialization.md`
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
  - `src/server/world.c`
  - `tests/test_perf_unit_protocol.c`
  - `tests/test_performance_eval.c`
  - `tests/test_protocol_edge.c`

## Prior Handoff Decision

- Rejected for this run after fresh measurement.
- The handoff recommended targeting raw-grid encode/decode per-cell overhead directly.
- Fresh research showed decode is not on the active server broadcast path and a decode-side helper trial regressed the focused protocol unit benchmark, so it is not the best next keep right now.

## Research Topics

1. Current repo state after three transport keeps.
   - Takeaway: the branch is ahead by three commits and still contains unrelated user docs/script edits that must be preserved.
   - Sources: `git status --short --branch`

2. Latest transport handoff recommendation.
   - Takeaway: previous run suggested raw-grid encode/decode per-cell overhead as the next narrow target.
   - Sources: `docs/agent-handoffs/latest.md`

3. Fresh broad baseline on current HEAD.
   - Takeaway: current medians are `12.90 ms` protocol serialize only, `14.16 ms` protocol deserialize only, `16.51 ms` build+serialize, `16.49 ms` end-to-end.
   - Sources: repeated `FEROX_PERF_SCALE=5 ./build/tests/test_performance_eval`

4. Fresh focused raw-grid baseline.
   - Takeaway: `test_perf_unit_protocol` currently reports noisy raw-mode medians around `0.358 ns/cell` serialize and `0.041 ns/cell` deserialize for `4096`, and `0.313 ns/cell` serialize and `0.038 ns/cell` deserialize for `65536`.
   - Sources: repeated `./build/tests/test_perf_unit_protocol`

5. Broad metric relevance.
   - Takeaway: only serialization affects the server broadcast lane directly; deserialize work matters mainly for protocol diagnostics and clients.
   - Sources: `tests/test_performance_eval.c`, `src/server/server.c`

6. Raw-mode trigger prevalence.
   - Takeaway: noisy grids in `test_perf_unit_protocol` already fall into raw mode with payload ratio about `1.000`, so raw-mode micro-optimizations are measurable there.
   - Sources: `tests/test_perf_unit_protocol.c`

7. Current raw encode loop shape.
   - Takeaway: raw-mode encoding writes one `uint16_t` at a time through `write_u16()`, which calls `htons()` plus `memcpy()` per cell.
   - Sources: `src/shared/protocol.c`

8. Current raw decode loop shape.
   - Takeaway: raw-mode decode mirrors that pattern with one `read_u16()` call per cell.
   - Sources: `src/shared/protocol.c`

9. Shared helper structure from last run.
   - Takeaway: `protocol_serialize_grid_rle_into()` now centralizes raw/RLE selection, making encoder-specific micro-experiments easier than broader serializer rewrites.
   - Sources: `src/shared/protocol.c`

10. Header write cost scale.
    - Takeaway: world-state headers and colony serialization still exist, but raw grid loops dominate the noisy-grid unit lanes because payload bytes scale with cell count.
    - Sources: `src/shared/protocol.c`, `tests/test_perf_unit_protocol.c`

11. Temporary allocation state after last run.
    - Takeaway: the extra staging buffer is already gone from `protocol_serialize_world_state()`, so the next transport-side win likely needs to come from per-cell work, not buffer lifetime.
    - Sources: `src/shared/protocol.c`, `docs/PERFORMANCE.md`

12. Decode-side helper candidate.
    - Takeaway: a direct raw decode helper using `memcpy` + `ntohs` per cell looks superficially cleaner, but it does not remove the byte-swap work and may inhibit compiler optimization.
    - Sources: `src/shared/protocol.c`, external `byteorder(3)`, `memcpy` docs

13. External byte-order guidance.
    - Takeaway: `htons`/`ntohs` are the canonical host/network conversion primitives and remain necessary for portable wire format correctness.
    - Sources: <https://www.man7.org/linux/man-pages/man3/htons.3.html>

14. External memcpy guidance.
    - Takeaway: `memcpy` is fast but not magic; replacing one small `memcpy` with another does not guarantee a win.
    - Sources: <https://en.cppreference.com/w/c/string/byte/memcpy>

15. Branch behavior in raw vs RLE mode.
    - Takeaway: raw mode has a simple predictable branch on `mode == 1`, so major branch-prediction wins are unlikely there; data movement is the real cost.
    - Sources: `src/shared/protocol.c`, branch predictor overview

16. RLE path branchiness.
    - Takeaway: RLE encode has more data-dependent branching than raw mode, but current noisy-grid measurements show raw mode is the more active concern for the chosen transport slice.
    - Sources: `src/shared/protocol.c`, `tests/test_perf_unit_protocol.c`

17. Layer-condition/locality relevance.
    - Takeaway: protocol grid loops are linear row-major scans over contiguous buffers, so locality is already favorable and leaves less room for tiling-style wins.
    - Sources: RRZE layer-condition notes, `src/shared/protocol.c`

18. Candidate experiment: decode helper.
    - Takeaway: low implementation effort, but lower relevance to the server-side broadcast path.
    - Sources: `src/shared/protocol.c`, `tests/test_perf_unit_protocol.c`

19. Candidate experiment: encode raw bulk write.
    - Takeaway: likely more promising than decode because serialization is in the active transport lane, but endian-safe bulk conversion needs careful implementation.
    - Sources: `src/shared/protocol.c`, `tests/test_performance_eval.c`

20. Candidate experiment: client-side/noisy decode optimization.
    - Takeaway: could help `protocol deserialize only`, but not the zero-client end-to-end benchmark emphasized by recent keeps.
    - Sources: `tests/test_performance_eval.c`, `src/client/client.c`

21. Candidate experiment: benchmark-only instrumentation expansion.
    - Takeaway: adding raw-vs-RLE world-state counters might improve observability, but it would not itself be a measured performance experiment.
    - Sources: `tests/test_performance_eval.c`, `docs/DEVELOPMENT_CYCLE.md`

22. Trial implementation result.
    - Takeaway: replacing the raw decode loop with a small helper plus `memcpy`/`ntohs` regressed focused unit medians materially (`noisy_large de` from about `0.038` to `0.053 ns/cell`) and also hurt serialize metrics on the same test binary, likely due to compiler/code-layout side effects.
    - Sources: trial branch local measurements from this run

23. Reversibility requirement.
    - Takeaway: because the trial did not improve measured results, experiment code must not be kept; only notebook/handoff/docs should remain.
    - Sources: run contract

24. Better next slice after rejection.
    - Takeaway: a future protocol experiment should probably target raw encode, not decode, and should prove itself first on `test_perf_unit_protocol` before using `test_performance_eval` as corroboration.
    - Sources: this run's measurements and code review

## Candidate Experiment Ideas

1. Raw-grid decode helper or loop cleanup.
   - Pros: small patch, easy to test.
   - Cons: weak connection to broadcast path; trial regressed.

2. Raw-grid encode fast path.
   - Pros: aligns with active `protocol serialize only` and `build+serialize` metrics.
   - Cons: endian-safe conversion needs more care than decode cleanup.

3. Extra benchmark instrumentation for raw-vs-RLE share.
   - Pros: useful for future targeting.
   - Cons: docs/observability only, not a direct performance experiment.

4. Client-side deserialize specialization.
   - Pros: may lower `protocol deserialize only`.
   - Cons: out of lane for zero-client server broadcast measurements.

## Triage Reasoning

- Idea 1 was the narrowest to try first, so this run tested it.
- The focused protocol-unit benchmark rejected it clearly enough that it should not be kept.
- Idea 2 remains the most promising next real experiment because serialization still matters to the active transport lane and avoids spending another cycle on decode-only cleanup.

## Chosen Hypothesis

If the raw-grid decode loop in `protocol_deserialize_grid_rle()` is refactored into a direct helper using contiguous reads, `test_perf_unit_protocol` noisy-grid deserialize medians will improve without changing roundtrip correctness.

## Experiment

- Tried a helper-based raw-mode decode path in `src/shared/protocol.c` that read contiguous `uint16_t` network-order cells and converted them to host order.
- Temporarily added a larger raw-mode roundtrip case in `tests/test_protocol_edge.c` to stress the decode path more explicitly.

## Tests Run

1. Build command:

```bash
cmake --build build -j4 --target test_protocol_edge test_perf_unit_protocol test_performance_eval
```

2. Correctness test:

```bash
./build/tests/test_protocol_edge
```

3. Focused benchmark command used for evaluation:

```bash
python3 - <<'PY'
import subprocess, re, statistics
pat = re.compile(r'UNIT_PROTOCOL kind=(\w+) size=(\d+) avg_bytes=([0-9.]+) ratio=([0-9.]+) ser_ns_cell=([0-9.]+) de_ns_cell=([0-9.]+)')
...
PY
```

## Benchmark Results

Baseline before trial:

- `noisy_small` median serialize: `0.358 ns/cell`
- `noisy_small` median deserialize: `0.041 ns/cell`
- `noisy_large` median serialize: `0.313 ns/cell`
- `noisy_large` median deserialize: `0.038 ns/cell`

Trial result before revert:

- `noisy_small` median serialize: `0.529 ns/cell`
- `noisy_small` median deserialize: `0.073 ns/cell`
- `noisy_large` median serialize: `0.447 ns/cell`
- `noisy_large` median deserialize: `0.053 ns/cell`

## Interpretation

- The hypothesis was not supported.
- The focused unit benchmark regressed noticeably, so the experiment code was reverted.
- The regression was strong enough that broad-system benchmarking was not justified after the focused gate failed.

## Revert Outcome

- Reverted all experiment code/test changes introduced in this run.
- Preserved this notebook and updated handoff/permanent docs so the next agent can avoid retrying the same losing slice blindly.

## Docs Updated

- `docs/PERFORMANCE_RESEARCH.md`
- `docs/agent-handoffs/latest.md`
- `docs/lab-notes/20260324-172911-raw-grid-decode-research.md`

## Next Recommended Experiment

Target raw-grid encode overhead rather than raw-grid decode cleanup; prove any candidate first with `test_perf_unit_protocol` noisy-grid serialize medians before checking broader `test_performance_eval` transport metrics.
