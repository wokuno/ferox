# Agent Handoff

- Run label: `20260324-184054-reset-command-feedback`
- Outcome: kept experiment; reset requests now return immediate structured `MSG_ACK` / `MSG_ERROR` feedback and clear stale client selection state on accepted rebuilds.
- Hypothesis: if `CMD_RESET` reuses `ProtoCommandStatus` over `MSG_ACK` / `MSG_ERROR`, then clients and focused correctness tests can distinguish accepted versus failed world resets immediately and clear stale local selection/detail state on success instead of relying on later snapshot side effects.

## What Changed

- Updated `src/server/server.c` so `CMD_RESET` now:
  - routes through a narrow `server_reset_world()` helper
  - returns `MSG_ACK` with `PROTO_COMMAND_STATUS_ACCEPTED` on successful world rebuild
  - returns `MSG_ERROR` with `PROTO_COMMAND_STATUS_INTERNAL_ERROR` if the rebuild fails
  - clears all connected clients' `selected_colony` values immediately after a successful reset so later broadcasts do not chase stale ids
- Updated terminal and GUI clients so reset acknowledgements clear stale local selection/detail state immediately:
  - `src/client/client.c`
  - `src/gui/gui_client.c`
- Updated focused coverage in:
  - `tests/test_server_branch_coverage.c`
  - `tests/test_phase5.c`
  - `tests/test_phase6.c`
  - `tests/test_client_logic_surface.c`
  - `tests/test_protocol_edge.c`
- Updated docs:
  - `docs/PROTOCOL.md`
  - `docs/ARCHITECTURE.md`
  - `docs/TESTING.md`
  - `docs/PERFORMANCE_RESEARCH.md`
  - `docs/lab-notes/20260324-184054-reset-command-feedback.md`

## Validation

- Focused correctness suite passed:
  - `ctest --test-dir build --output-on-failure -R "ProtocolEdgeTests|ServerBranchCoverageTests|Phase5Tests|Phase6Tests"`
- Additional client-surface test passed:
  - `./build/tests/test_client_logic_surface`
- Focused rebuild passed:
  - `cmake --build build -j4 --target test_protocol_edge test_server_branch_coverage test_phase5 test_phase6`

## Notes For Next Agent

- There are unrelated pre-existing user changes in docs/scripts/root files; do not revert them.
- `ProtoCommandStatus` is now live for manual spawn, colony selection, and reset.
- Pause/resume and speed-control commands still rely on optimistic local client state with no structured ack for clamp/no-op outcomes.
- `tests/test_client_logic_surface.c` is still a standalone manually compiled surface test rather than a CMake target.

## Recommended Next Experiment

Extend the command-status surface to speed-limit clamping or pause/resume so optimistic local playback state can be reconciled immediately rather than waiting for later snapshots.
