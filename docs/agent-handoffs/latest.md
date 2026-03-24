# Agent Handoff

- Run label: `20260324-171905-one-pass-world-state-serialization`
- Outcome: committed improvement candidate; inline-grid world-state serialization stayed green and showed repeated-run median improvement in the broader transport lane.
- Hypothesis: writing encoded grid payloads directly into the final world-state output buffer would reduce serialization overhead by removing a temporary allocation and copy stage.

## What Changed

- `src/shared/protocol.c`
  - `protocol_serialize_world_state()` now reserves enough space for the useful worst-case grid payload and encodes directly into the final output buffer.
  - Shared grid-encoding helpers now support both direct-to-buffer world-state serialization and the standalone `protocol_serialize_grid_rle()` API.
- `tests/test_protocol_edge.c`
  - Added `world_state_with_noisy_grid_roundtrip_preserves_cells` to cover the raw-mode-heavy world-state path.
- Docs updated:
  - `docs/PERFORMANCE.md`
  - `docs/PERFORMANCE_RESEARCH.md`
  - `docs/lab-notes/20260324-171905-one-pass-world-state-serialization.md`

## Validation

- Tests passed:
  - `./build/tests/test_protocol_edge`
  - `./build/tests/test_perf_unit_protocol`
- Benchmarks executed after tests passed (`FEROX_PERF_SCALE=5`, `./build/tests/test_performance_eval`, `3` runs):
  - baseline `protocol serialize only`: `13.27`, `13.75`, `13.85 ms` (median `13.75 ms`)
  - candidate `protocol serialize only`: `14.12`, `12.91`, `13.25 ms` (median `13.25 ms`)
  - baseline `broadcast build+serialize`: `16.99`, `17.66`, `18.05 ms` (median `17.66 ms`)
  - candidate `broadcast build+serialize`: `16.59`, `16.48`, `17.32 ms` (median `16.59 ms`)
  - baseline `broadcast end-to-end (0 clients)`: `17.16`, `17.90`, `18.04 ms` (median `17.90 ms`)
  - candidate `broadcast end-to-end (0 clients)`: `16.50`, `16.51`, `17.20 ms` (median `16.51 ms`)

## Notes For Next Agent

- The transport path now has three consecutive narrow keeps: lookup indirection removal, inline-grid zero-fill removal, and one-pass world-state serialization.
- There are unrelated pre-existing user changes in docs/scripts/root files; do not revert them.
- This keep is real but smaller/noisier than the prior two transport passes, so the next experiment should still prefer a tightly measurable protocol micro-path.

## Recommended Next Experiment

Target the raw-grid path inside `protocol_serialize_grid_rle()` or `protocol_deserialize_grid_rle()` to reduce per-cell byte-order write/read overhead now that the extra staging buffer is gone.
