# Agent Handoff

- Run label: `20260324-171525-broadcast-inline-grid-allocation`
- Outcome: committed improvement candidate; broader broadcast-path follow-up stayed green and showed repeated-run median improvement.
- Hypothesis: allocating the inline snapshot grid directly inside `server_build_protocol_world_snapshot()` would reduce broader broadcast costs by removing a redundant zero-fill before the grid-copy pass overwrites every cell.

## What Changed

- `src/server/server.c`
  - `server_build_protocol_world_snapshot()` now allocates inline snapshot grids directly with `malloc` and sets `grid_size` / `has_grid` explicitly.
  - This removes the eager `memset` previously done by `proto_world_alloc_grid()` on a hot path that immediately rewrites the full grid.
- `tests/test_protocol_edge.c`
  - Added `world_state_with_grid_roundtrip_preserves_cells` so populated world-state grids still round-trip through serialization and deserialization.
- Docs updated:
  - `docs/PERFORMANCE.md`
  - `docs/PERFORMANCE_RESEARCH.md`
  - `docs/lab-notes/20260324-171525-broadcast-inline-grid-allocation.md`

## Validation

- Tests passed:
  - `./build/tests/test_protocol_edge`
  - `./build/tests/test_phase2`
- Benchmarks executed after tests passed (`FEROX_PERF_SCALE=5`, `./build/tests/test_performance_eval`, `3` runs):
  - baseline `broadcast build snapshot`: `9.56`, `9.29`, `9.41 ms` (median `9.41 ms`)
  - candidate `broadcast build snapshot`: `7.01`, `6.98`, `7.26 ms` (median `7.01 ms`)
  - baseline `broadcast build+serialize`: `20.62`, `20.47`, `21.27 ms` (median `20.62 ms`)
  - candidate `broadcast build+serialize`: `17.01`, `17.05`, `18.02 ms` (median `17.05 ms`)
  - baseline `broadcast end-to-end (0 clients)`: `20.38`, `20.22`, `21.41 ms` (median `20.38 ms`)
  - candidate `broadcast end-to-end (0 clients)`: `17.22`, `17.17`, `17.59 ms` (median `17.22 ms`)

## Notes For Next Agent

- The previous focused snapshot lookup win now has broader transport corroboration on this host.
- There are unrelated pre-existing user changes in docs/scripts/root files; do not revert them.
- No revert is needed for this run because every candidate broadcast run beat every baseline broadcast run.

## Recommended Next Experiment

Target `protocol_serialize_world_state()` in the inline-grid path to reduce serialization overhead now that `broadcast build snapshot` has dropped again. The most promising narrow slices are temporary allocation/copy reduction around the encoded buffer or the grid raw-vs-RLE decision path.
