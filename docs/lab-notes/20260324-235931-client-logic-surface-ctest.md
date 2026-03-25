# Lab Notebook: Client Logic Surface CTest Integration

- **Run label**: `20260324-235931-client-logic-surface-ctest`
- **Date**: 2026-03-24T23:59:31Z

## Starting Context

Reviewed before changes:
- `git status --short --branch`
- `docs/agent-handoffs/latest.md`
- recent notebooks in `docs/lab-notes/`
- required docs: `README.md`, `docs/DEVELOPMENT_CYCLE.md`, `docs/TESTING.md`, `docs/SIMULATION.md`, `docs/COLONY_INTELLIGENCE.md`, `docs/GENETICS.md`, `docs/SCALING_AND_BEHAVIOR_PLAN.md`, `docs/ARCHITECTURE.md`, `docs/PERFORMANCE.md`, `docs/PERFORMANCE_RESEARCH.md`, `docs/PERF_RUNBOOK.md`, `docs/SCIENCE_BENCHMARKS.md`, `docs/CODEBASE_REVIEW_LOG.md`
- target subsystem files: `tests/CMakeLists.txt`, `tests/test_client_logic_surface.c`, `src/client/client.c`, `src/client/client.h`, `src/client/input.h`, `src/client/renderer.h`, `src/client/CMakeLists.txt`

Pre-existing unrelated worktree changes were left untouched, including user edits in root docs and `tests/CMakeLists.txt`.

## Research Topics (22)

1. **CTest matrix lives entirely in `tests/CMakeLists.txt`**
   - Takeaway: a client surface test only needs CMake registration to become visible to `ctest`.
   - Source: `tests/CMakeLists.txt`
2. **`test_client_logic_surface.c` is already a full single-translation-unit harness**
   - Takeaway: it includes `../src/client/client.c` directly and is intentionally not a normal linked unit test.
   - Source: `tests/test_client_logic_surface.c:270`
3. **The harness already stubs renderer entry points**
   - Takeaway: no renderer library link is required for the surface test.
   - Source: `tests/test_client_logic_surface.c:71`
4. **The harness already stubs network entry points**
   - Takeaway: no socket setup or `ferox_shared` network linkage is required.
   - Source: `tests/test_client_logic_surface.c:131`
5. **The harness already stubs protocol encode/decode helpers**
   - Takeaway: normal protocol object linking would be redundant and risks duplicate symbols.
   - Source: `tests/test_client_logic_surface.c:152`, `tests/test_client_logic_surface.c:193`, `tests/test_client_logic_surface.c:203`
6. **The harness already stubs world-grid helper functions**
   - Takeaway: chunked-grid code paths can compile without linking the shared protocol implementation.
   - Source: `tests/test_client_logic_surface.c:227`, `tests/test_client_logic_surface.c:237`, `tests/test_client_logic_surface.c:248`
7. **The test reaches static client helpers by inclusion**
   - Takeaway: `client_process_input()` and other file-local logic are only reachable because `client.c` is included, not linked.
   - Source: `src/client/client.c:314`, `tests/test_client_logic_surface.c:270`
8. **`client.c` depends only on declarations that the harness already satisfies**
   - Takeaway: CMake can compile the test source by itself without `ferox_client_lib`.
   - Source: `src/client/client.c`
9. **`src/client/CMakeLists.txt` builds `ferox_client_lib` from `client.c`, `input.c`, and `renderer.c`**
   - Takeaway: linking that library into the surface test would duplicate the included `client.c` symbols.
   - Source: `src/client/CMakeLists.txt:1`
10. **The client surface test covers selection cycling**
    - Takeaway: it is broader than a narrow input-only test and is worth putting in the matrix.
    - Source: `tests/test_client_logic_surface.c:280`
11. **The client surface test covers command-status message handling**
    - Takeaway: it protects recent selection/reset/spawn feedback behavior.
    - Source: `tests/test_client_logic_surface.c:354`, `tests/test_client_logic_surface.c:377`
12. **The client surface test covers optimistic pause and speed controls**
    - Takeaway: it guards the client-side command UX surface, not just deserialization.
    - Source: `tests/test_client_logic_surface.c:443`
13. **The client surface test prints a stable success line**
    - Takeaway: it is already shaped like the other standalone tests and fits CTest well.
    - Source: `tests/test_client_logic_surface.c:539`
14. **Two other surface tests follow the same inclusion pattern**
    - Takeaway: `test_client_input_surface.c` and `test_renderer_logic_surface.c` are future candidates, but adding all three at once would violate the one-experiment rule.
    - Source: `tests/test_client_input_surface.c:11`, `tests/test_renderer_logic_surface.c:10`
15. **The latest handoff specifically recommended this wiring task**
    - Takeaway: the idea is advisory but still valid after fresh code review.
    - Source: `docs/agent-handoffs/latest.md:30`
16. **The previous notebook already documented manual standalone compilation**
    - Takeaway: the test works today, but only outside the CTest matrix.
    - Source: `docs/lab-notes/20260324-233546-client-speed-factor-alignment.md:102`
17. **`docs/TESTING.md` still describes a 27-target matrix**
    - Takeaway: the permanent test docs need an update if a new CTest target lands.
    - Source: `docs/TESTING.md:8`
18. **This experiment is tooling/correctness, not performance**
    - Takeaway: benchmarks are not required; the measurable outcome is successful CMake/CTest integration.
    - Source: `docs/DEVELOPMENT_CYCLE.md:93`, `docs/PERF_RUNBOOK.md:141`
19. **The repo currently has a persistent `build/` tree**
    - Takeaway: reconfiguring and building a single new test target is cheap and reversible.
    - Source: `build/` listing and existing CMake build tree
20. **`tests/CMakeLists.txt` already contains unrelated user edits**
    - Takeaway: the experiment must append a minimal registration block without disturbing nearby changes.
    - Source: `git status --short --branch`, `git diff -- tests/CMakeLists.txt`
21. **The client test does not need extra include directories to reach its dependencies**
    - Takeaway: relative includes and the included file paths are already self-contained.
    - Source: `tests/test_client_logic_surface.c`, `src/client/client.c`
22. **CTest naming convention uses descriptive CamelCase names ending in `Tests`**
    - Takeaway: `ClientLogicSurfaceTests` fits the existing matrix style.
    - Source: `tests/CMakeLists.txt`

## Candidate Experiment Ideas

1. **Register `test_client_logic_surface.c` as a standalone CTest target**
   - Narrow, reversible, measurable via configure/build/ctest.
2. **Also register `test_client_input_surface.c` and `test_renderer_logic_surface.c`**
   - Attractive follow-up, but too broad for a one-experiment run.
3. **Refactor client logic into a separately linkable compilation unit**
   - Higher risk and unnecessary if the current harness compiles cleanly as-is.
4. **Add structured feedback for pause/resume/speed commands**
   - Valid correctness topic, but outside the current tooling focus.
5. **Clean up stale testing docs only**
   - Documentation-only, not an experiment.

## Triage Reasoning

- Idea 1 wins because it keeps the current test design intact, adds immediate regression coverage to `ctest`, and does not require touching client runtime code.
- Idea 2 is deferred because expanding scope to multiple surface tests would turn one narrow slice into a broader test-matrix sweep.
- Idea 3 is rejected for this run because the single-translation-unit harness already works and broader refactoring is not needed to prove the hypothesis.

## Chosen Hypothesis

If `tests/test_client_logic_surface.c` is registered in CMake as its own standalone executable without linking `ferox_client_lib`, then the existing single-translation-unit harness will build and run under CTest, making client-side logic regressions visible in the automated test matrix.

## Implementation Changes

### `tests/CMakeLists.txt`
- Added a dedicated `test_client_logic_surface` executable.
- Registered it with CTest as `ClientLogicSurfaceTests`.
- Deliberately did not link `ferox_client_lib` so the existing `#include "../src/client/client.c"` harness remains the single translation unit.

### `docs/TESTING.md`
- Updated the documented CTest target count.
- Added `ClientLogicSurfaceTests` to the advanced correctness/stability list.
- Added a focused `ctest -R` example that includes the new target.

### `docs/agent-handoffs/latest.md`
- Replaced the prior handoff with the result of this run and a next-step recommendation.

## Tests Run

### Configure
```bash
cmake -S . -B build
```
Result: passed

### Build the new target
```bash
cmake --build build -j4 --target test_client_logic_surface
```
Result: passed

### Focused CTest slice
```bash
ctest --test-dir build --output-on-failure -R "ClientLogicSurfaceTests|Phase5Tests|Phase6Tests"
```
Result: passed

### Verify matrix registration
```bash
ctest --test-dir build -N
```
Result: `ClientLogicSurfaceTests` listed in the matrix; target count increased by one.

## Benchmarks

Not run. This experiment changes test registration only and makes no performance claim.

## Interpretation

Hypothesis supported. The existing client surface harness compiles cleanly under CMake as a standalone executable and now runs through `ctest` without refactoring client code.

This is a kept improvement because:
- it increases automated regression coverage for client command/status/input logic
- it preserves the current low-friction harness design
- it required only minimal, reversible test-matrix wiring

## Next Recommended Experiment

Promote one additional standalone client surface harness into CTest next, likely `tests/test_client_input_surface.c`, so terminal-input regressions also stop depending on manual compilation.
