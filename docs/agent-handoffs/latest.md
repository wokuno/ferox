# Agent Handoff

- Run label: `20260324-235931-client-logic-surface-ctest`
- Outcome: kept experiment; `test_client_logic_surface.c` now runs inside the CMake/CTest matrix as `ClientLogicSurfaceTests`.
- Hypothesis: if the existing single-translation-unit client surface harness is compiled by CMake without linking `ferox_client_lib`, then it can run under CTest and catch client logic regressions automatically.

## What Changed

- Added a standalone `test_client_logic_surface` executable in `tests/CMakeLists.txt`.
- Registered the executable with CTest as `ClientLogicSurfaceTests`.
- Updated `docs/TESTING.md` so the documented matrix count and example commands include the new target.

## Validation

- `cmake -S . -B build`: passed
- `cmake --build build -j4 --target test_client_logic_surface`: passed
- `ctest --test-dir build --output-on-failure -R "ClientLogicSurfaceTests|Phase5Tests|Phase6Tests"`: passed
- `ctest --test-dir build -N`: shows `ClientLogicSurfaceTests` in the matrix

## Notes For Next Agent

- There are unrelated pre-existing user changes in docs/scripts/root files; do not revert them.
- `test_client_logic_surface.c` still uses the `#include ../src/client/client.c` harness pattern by design; do not link it against `ferox_client_lib` unless you refactor the test architecture intentionally.
- `test_client_input_surface.c` and `test_renderer_logic_surface.c` use the same surface-test style and remain outside CTest.
- CMD_PAUSE/RESUME/SPEED_UP/SLOW_DOWN still do not send structured feedback; they remain optimistic fire-and-forget commands.
- One older handoff note was stale: `docs/PROTOCOL.md` already documents `CMD_RESET` with ACK/ERROR behavior.

## Recommended Next Experiment

Promote one more standalone client surface harness into CTest, most likely `tests/test_client_input_surface.c`, so terminal-input regressions also become part of the automated matrix.
