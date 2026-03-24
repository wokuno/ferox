# Agent Handoff

- Run label: `20260324-233546-client-speed-factor-alignment`
- Outcome: kept experiment; client speed adjustment factor aligned with server (2.0x instead of 1.5x) to eliminate optimistic display mismatch.
- Hypothesis: if the client's speed adjustment factor matches the server's (2.0x), the displayed speed between user action and next server broadcast will be correct.

## What Changed

- Fixed speed multiplier factor in both clients from `*= 1.5f` / `/= 1.5f` to `*= 2.0f` / `/= 2.0f`:
  - `src/client/client.c`
  - `src/gui/gui_client.c`
- Added factor-matching assertions to both test files:
  - `tests/test_client_logic_surface.c` — verifies 1.0→2.0 on speed-up and 2.0→1.0 on slow-down
  - `tests/test_server_branch_coverage.c` — adds `ASSERT_FLOAT_EQ` macro and server-side factor checks

## Validation

- Full ctest suite: 27/27 passed
- Standalone client surface test: 8/8 passed
- Focused suite (Phase5, Phase6, ServerBranch, ProtocolEdge): 4/4 passed

## Notes For Next Agent

- There are unrelated pre-existing user changes in docs/scripts/root files; do not revert them.
- `test_client_logic_surface.c` is still a standalone manually compiled test, not a CMake target. It `#include`s `../src/client/client.c` directly, which conflicts with normal library linking.
- CMD_PAUSE/RESUME/SPEED_UP/SLOW_DOWN still do not send structured feedback; these are trivially successful fire-and-forget commands.
- PROTOCOL.md still says `CMD_RESET | none` for response payload, but the code now sends ACK/ERROR. Updating that doc is a separate task.
- The speed sequence from server baseline 1.0x is: 2.0, 4.0, 8.0, 10.0 (clamped). Both client and server now produce the same sequence.

## Recommended Next Experiment

Wire `test_client_logic_surface.c` into the CMake test matrix as a proper CTest target so client-side logic regressions are caught by `ctest` automatically. This requires solving the `#include client.c` pattern — either by compiling the test as a single translation unit or by extracting testable client logic into a separate compilation unit.
