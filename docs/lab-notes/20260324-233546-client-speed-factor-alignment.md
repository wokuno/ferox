# Lab Notebook: Client Speed Factor Alignment

- **Run label**: `20260324-233546-client-speed-factor-alignment`
- **Date**: 2026-03-24T23:35:46Z

## Starting Context

Reviewed:
- Git history (HEAD at `ed494b4`): 7 prior agent runs — 3 transport perf, 1 reverted decode, 3 protocol feedback.
- Latest handoff: recommended extending command-status to CMD_RESET or speed-limit clamping.
- Lab notes: All 7 prior runs reviewed, showing a trend from perf to correctness.
- Core docs: README, SIMULATION, GENETICS, COLONY_INTELLIGENCE, ARCHITECTURE, PERFORMANCE, PERFORMANCE_RESEARCH, PERF_RUNBOOK, SCALING_AND_BEHAVIOR_PLAN, SCIENCE_BENCHMARKS, TESTING, DEVELOPMENT_CYCLE, CODEBASE_REVIEW_LOG.

## Prior Handoff Decision

**Rejected** the prior recommendation to generalize command-status to CMD_RESET.
Reason: CMD_RESET already received `ProtoCommandStatus` feedback via `server_reset_world()` in the most recent commit (`ed494b4`). The handoff was written before that addition. CMD_PAUSE/RESUME/SPEED are trivially successful commands where structured feedback adds little value.

## Research Topics (22 topics)

### Simulation Mechanics (6 topics)
1. **Mutation mechanics** — Fully implemented with multi-level rates (MUTATE_FIELD vs MUTATE_FIELD_SLOW). Covers 25+ genome fields including behavior graph weights. Source: genetics.c:206-305.
2. **Horizontal gene transfer** — Fully implemented via `genome_transfer_genes()` (genetics.c:502-538). Called from `simulation_apply_horizontal_gene_transfer()` (simulation.c:1666-1714). Transfers toxin resistance, nutrient sensitivity, efficiency, dormancy resistance, and behavior graph modules.
3. **Quorum sensing** — Active mechanic, not stub. `QUORUM_SENSING_RADIUS = 3`, modulates spread via density penalty in simulation_common.c.
4. **Combat system** — 2-pass (toxin emission → combat resolution) with strategic modifiers: flanking bonus, size ratio, behavior actions, toxin factors, stress penalties. simulation.c:1869-2100+.
5. **Colony division** — Fully functional via connected-component detection. Only largest component stays; fragments ≥5 cells become new colonies with mutated genomes. One division per tick max.
6. **Environmental layers** — All actively simulated: nutrients (consumption + regen), toxins (emission + combat + decay), signals (diffusion + source tracking), alarm signals (stress-driven, fast decay).

### Protocol & Transport (4 topics)
7. **Protocol message types** — 8 types defined. MSG_WORLD_DELTA is chunked grid (not true delta). No real incremental compression.
8. **Command feedback completeness** — CMD_SPAWN_COLONY, CMD_SELECT_COLONY, CMD_RESET all have MSG_ACK/MSG_ERROR. CMD_PAUSE/RESUME/SPEED_UP/SLOW_DOWN still lack structured feedback.
9. **Client speed factor vs server factor** — **KEY FINDING**: Clients use `*= 1.5f` / `/= 1.5f` while server uses `*= 2.0f` / `/= 2.0f`. This causes the client's optimistic speed display to be wrong between user action and next server broadcast. Source: client.c:334, gui_client.c:413 vs server.c:1049.
10. **World state sync** — The deserialized world state includes `speed_multiplier` and `paused` from the server, so the client eventually corrects itself. But the mismatch window creates visible display errors.

### Testing & Correctness (5 topics)
11. **HGT test coverage** — Surprisingly thorough: 9 unit tests in test_genetics_advanced.c (null safety, strength levels, trait blending, behavior graph transfer) + 1 integration test in test_simulation_logic.c.
12. **Colony death tests** — Tested in test_combat_system.c (starvation, toxin damage, natural aging) and test_simulation_stress.c (growth-death cycles, full extinction).
13. **Biofilm tests** — Tested implicitly in combat, dormancy, and protocol tests. Not directly unit-tested for strength mechanics.
14. **Dormancy state transitions** — One focused test: `colony_dynamics_enters_dormancy_under_stress`. Minimal but functional.
15. **Client logic surface test** — Standalone file that `#include`s client.c directly and provides stubs for all external deps. Not in CMake targets but covers client input processing, message handling, and selection logic.

### Architecture & Scaling (3 topics)
16. **Atomic simulation path** — Parallel age + spread phases with serial maintenance (mutations, divisions, combat, HGT) every N ticks. Double-buffered lock-free grid with CAS claims.
17. **Hardware detection** — Probes CPU/GPU, selects runtime target (cpu/apple/amd), tunes scheduler profile and atomic cadence.
18. **Threadpool work distribution** — Single global queue with per-worker local state, work stealing, 4096 free task limit. Known bottleneck: single queue_mutex.

### Client/UX & Documentation (4 topics)
19. **Renderer capabilities** — Terminal (ANSI) and GUI (SDL2) with zoom, pan, grid overlay, colony detail panel with behavior graph stats, status bar.
20. **Feroxclub dashboard** — Go-based local dashboard at :8787 with live preview GIF, run summaries, event timeline, transcript display.
21. **PROTOCOL.md accuracy** — Documents CMD_RESET as having "none" payload response, but code now sends ACK/ERROR. Needs update.
22. **Science benchmarks** — 6 canonical scenarios defined in JSON with Python runner script. Specifications with pass-bands, not yet validated against live simulation.

## Candidate Experiment Ideas

1. **Fix client speed factor mismatch** (1.5x → 2.0x) — Correctness fix. Client displays wrong speed between user action and next server sync. Measurable via test assertions. Low risk, 4 lines of code.
2. **Add CMD_PAUSE/RESUME structured feedback** — Protocol consistency. But these commands never fail; overhead without clear benefit.
3. **Wire test_client_logic_surface.c into CMake** — Tooling improvement. Complex due to #include client.c pattern conflicting with library linking.
4. **Update PROTOCOL.md for CMD_RESET feedback** — Documentation fix, not an experiment.
5. **Validate science benchmark pass-bands** — Would test simulation correctness against literature-grounded metrics. Complex setup, uncertain if runner works.
6. **Environment-modifier precomputation** — Performance: precompute local density once per colony. High impact but complex.

## Triage Reasoning

- **Idea 1 (speed factor)** wins: measurable, falsifiable, correctness improvement, minimal risk, non-performance category, addresses a real user-visible bug.
- **Idea 2** rejected: PAUSE/RESUME never fail, so ACK adds protocol noise without value.
- **Idea 3** rejected: interesting but complex (the test #includes client.c directly, so normal CMake linking conflicts).
- **Idea 4** is documentation, not an experiment.
- **Idea 5** rejected: setup complexity too high for this run.
- **Idea 6** rejected: performance-only, and prior runs are heavy on perf.

## Chosen Hypothesis

If the client's optimistic speed adjustment factor is changed from 1.5x to 2.0x to match the server's factor, then the speed displayed between user action and next server broadcast will be correct, eliminating the speed display mismatch.

## Implementation Changes

### src/client/client.c
- Line 334: `*= 1.5f` → `*= 2.0f` (speed up factor)
- Line 342: `/= 1.5f` → `/= 2.0f` (slow down factor)

### src/gui/gui_client.c
- Line 413: `*= 1.5f` → `*= 2.0f` (speed up factor)
- Line 421: `/= 1.5f` → `/= 2.0f` (slow down factor)

### tests/test_client_logic_surface.c
- Added speed factor verification tests: starting from 1.0f, speed up should give 2.0f; from 2.0f, slow down should give 1.0f.
- Retained existing bound-clamping tests.

### tests/test_server_branch_coverage.c
- Added `#include <math.h>` and `ASSERT_FLOAT_EQ` macro.
- Added server-side factor verification: from 1.0f, CMD_SPEED_UP gives 2.0f; CMD_SLOW_DOWN returns to 1.0f.
- Documents that server and client factors must match.

## Tests Run

### Focused suite
```
ctest --test-dir build --output-on-failure -R "Phase5Tests|Phase6Tests|ServerBranchCoverageTests|ProtocolEdgeTests"
```
Result: 4/4 passed (0.90 sec total)

### Standalone client surface test
```
cc -o build/test_client_logic_surface tests/test_client_logic_surface.c src/client/client.c -I src/shared -I src/client -lm
# ↑ fails with duplicate symbols (expected — test #includes client.c)
cc -o build/test_client_logic_surface tests/test_client_logic_surface.c -I src/shared -I src/client -lm
./build/test_client_logic_surface
```
Result: 8/8 tests passed

### Full test suite
```
ctest --test-dir build --output-on-failure
```
Result: 27/27 passed (29.36 sec total)

## Benchmarks

Not applicable — this is a correctness fix, not a performance change. The speed multiplier change affects a UI display value, not simulation throughput.

## Interpretation

**Hypothesis supported.** The client's optimistic speed display now matches the server's computed value in all tested cases:
- 1.0f × 2.0f = 2.0f (matches server)
- 2.0f / 2.0f = 1.0f (matches server)
- Clamping at 10.0f and 0.1f unchanged.

Previously, a user pressing speed-up from 1.0x would see "1.5x" displayed immediately, while the server was actually running at 2.0x. The next world_state broadcast would correct the display, but the brief flicker was misleading.

**Change kept.** All 27 CTest targets and 8 standalone surface tests pass.

## Next Recommended Experiment

Wire `test_client_logic_surface.c` into the CMake test matrix as a proper CTest target so client-side logic regressions are caught automatically. The current standalone compilation pattern works but is invisible to `ctest`.
