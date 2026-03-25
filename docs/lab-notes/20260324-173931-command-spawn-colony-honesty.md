# Lab Note 20260324-173931 command-spawn-colony-honesty

## Starting Context

- Run label: `20260324-173931-command-spawn-colony-honesty`
- Repository state reviewed with `git status --short --branch` before edits.
- Prior handoff reviewed at `docs/agent-handoffs/latest.md`.
- Prior notebooks reviewed:
  - `docs/lab-notes/20260324-165209-snapshot-colony-id-lookup.md`
  - `docs/lab-notes/20260324-171525-broadcast-inline-grid-allocation.md`
  - `docs/lab-notes/20260324-171905-one-pass-world-state-serialization.md`
  - `docs/lab-notes/20260324-172911-raw-grid-decode-research.md`
- Required docs reviewed:
  - `README.md`
  - `docs/DEVELOPMENT_CYCLE.md`
  - `docs/TESTING.md`
  - `docs/SIMULATION.md`
  - `docs/COLONY_INTELLIGENCE.md`
  - `docs/GENETICS.md`
  - `docs/SCALING_AND_BEHAVIOR_PLAN.md`
  - `docs/ARCHITECTURE.md`
  - `docs/PERFORMANCE.md`
  - `docs/PERFORMANCE_RESEARCH.md`
  - `docs/PERF_RUNBOOK.md`
  - `docs/SCIENCE_BENCHMARKS.md`
  - `docs/CODEBASE_REVIEW_LOG.md`
  - `docs/PROTOCOL.md`
- Subsystem code/tests reviewed:
  - `src/server/server.c`
  - `src/server/server.h`
  - `src/server/world.c`
  - `src/server/world.h`
  - `src/server/atomic_sim.c`
  - `src/shared/protocol.c`
  - `src/shared/protocol.h`
  - `src/shared/types.h`
  - `src/client/client.c`
  - `src/gui/gui_client.c`
  - `tests/test_server_branch_coverage.c`
  - `tests/test_phase4.c`
  - `tests/test_protocol_edge.c`
  - `tests/CMakeLists.txt`

## Research Topics

1. Current worktree safety.
   - Takeaway: there are unrelated user doc/script edits in the tree, so this run must avoid reverting them.
   - Sources: `git status --short --branch`

2. Latest handoff recommendation.
   - Takeaway: the last protocol micro-optimization regressed and should not be retried blindly.
   - Sources: `docs/agent-handoffs/latest.md`

3. Recent autonomous run pattern.
   - Takeaway: the recent kept runs were transport-heavy, so this run should choose a different subsystem if evidence supports it.
   - Sources: prior lab notes under `docs/lab-notes/`

4. Development-cycle requirement for narrow reversible slices.
   - Takeaway: the next change should be a small, evidence-backed behavior fix with matching docs/tests.
   - Sources: `docs/DEVELOPMENT_CYCLE.md`

5. Testing matrix expectations.
   - Takeaway: protocol and behavior changes should land with correctness coverage, not just implementation edits.
   - Sources: `docs/TESTING.md`

6. Architecture-level command surface.
   - Takeaway: docs still describe clients sending pause, speed, selection, and reset commands, but not a working manual spawn path.
   - Sources: `docs/ARCHITECTURE.md`

7. Protocol contract for `CMD_SPAWN_COLONY`.
   - Takeaway: the wire format already advertises spawn with `x`, `y`, and `name`, so the server should either implement it or stop claiming it.
   - Sources: `docs/PROTOCOL.md`, `src/shared/protocol.h`

8. Wire-format implementation parity.
   - Takeaway: protocol serialize/deserialize already fully supports `CMD_SPAWN_COLONY`; the mismatch is server-side behavior.
   - Sources: `src/shared/protocol.c`

9. Existing server command handling.
   - Takeaway: `CMD_SPAWN_COLONY` was only a log-print stub and produced no world change.
   - Sources: `src/server/server.c`

10. World colony creation primitive.
    - Takeaway: `world_add_colony()` already provides id allocation and registry updates, so spawn can be implemented with existing world APIs.
    - Sources: `src/server/world.c`, `src/server/world.h`

11. Cell tracking helper behavior.
    - Takeaway: `world_colony_add_cell()` updates tracked indices and centroid state, so using it keeps spawn aligned with other colony bookkeeping.
    - Sources: `src/server/world.c`

12. Atomic runtime coupling.
    - Takeaway: server commands mutate `World`, but the active runtime ticks from `AtomicWorld`, so command-side structural changes must sync back into the atomic mirror.
    - Sources: `src/server/atomic_sim.c`, `docs/ARCHITECTURE.md`

13. Tick-thread ordering.
    - Takeaway: the simulation thread runs `atomic_tick()`, then broadcast, then client command processing, so spawn effects should appear on the next tick after syncing.
    - Sources: `src/server/server.c`

14. Client command send paths.
    - Takeaway: both terminal and GUI clients can already send arbitrary commands using the shared command serializer.
    - Sources: `src/client/client.c`, `src/gui/gui_client.c`

15. Existing protocol tests.
    - Takeaway: spawn command coverage already exists for wire roundtrip but not for server semantics.
    - Sources: `tests/test_phase4.c`, `tests/test_protocol_edge.c`

16. Existing branch coverage gap.
    - Takeaway: the server branch coverage test touched `CMD_SPAWN_COLONY` but asserted nothing about world changes.
    - Sources: `tests/test_server_branch_coverage.c`

17. Code review backlog priority.
    - Takeaway: protocol honesty around `CMD_SPAWN_COLONY` is already listed as a review finding and immediate fix target.
    - Sources: `docs/CODEBASE_REVIEW_LOG.md`

18. Simulation docs scope.
    - Takeaway: dynamic colony creation already exists in simulation-side maintenance, so manual spawn is conceptually aligned with current model behavior.
    - Sources: `docs/SIMULATION.md`, `src/server/simulation.c`

19. Candidate area: raw encode perf follow-up.
    - Takeaway: still promising later, but it would continue the transport-only streak instead of addressing a known protocol-surface mismatch.
    - Sources: `docs/PERFORMANCE_RESEARCH.md`, `tests/test_perf_unit_protocol.c`

20. Candidate area: client UX around selection.
    - Takeaway: useful, but lower priority than fixing a documented server command that currently lies about capability.
    - Sources: `src/gui/gui_client.c`, `src/client/client.c`

21. Candidate area: command spawn semantics.
    - Takeaway: a minimal one-cell spawn on an empty in-bounds target is narrow, reversible, and directly falsifiable.
    - Sources: `src/server/server.c`, `src/server/world.c`

22. Bounds/occupancy guard behavior.
    - Takeaway: `world_get_cell()` cleanly rejects out-of-bounds positions and existing occupancy can reject overlapping manual spawns without extra global state.
    - Sources: `src/server/world.c`

23. Naming behavior.
    - Takeaway: the command payload already carries `name[32]`, and a blank name can safely fall back to the existing scientific name generator.
    - Sources: `src/shared/protocol.h`, `src/shared/names.h`, `src/server/world.c`

24. Validation scope for this run.
    - Takeaway: correctness tests are appropriate; repeated performance benchmarking is not necessary because this run targets protocol honesty rather than throughput.
    - Sources: `docs/TESTING.md`, `docs/DEVELOPMENT_CYCLE.md`

## Candidate Experiment Ideas

1. Implement minimal real `CMD_SPAWN_COLONY` support.
   - Pros: fixes a documented protocol mismatch with small scope.
   - Cons: needs careful `AtomicWorld` sync after mutation.

2. Remove `CMD_SPAWN_COLONY` from the protocol docs instead.
   - Pros: even smaller patch.
   - Cons: gives up an already-serialized capability and weakens the client/server command surface.

3. Add only stronger tests around the current stub.
   - Pros: exposes the gap immediately.
   - Cons: intentionally leaves the protocol surface dishonest.

4. Return explicit errors/acks for rejected commands.
   - Pros: better UX long term.
   - Cons: larger protocol-surface scope than one run needs.

## Triage Reasoning

- Idea 1 is the best narrow slice because it closes an already-documented mismatch without requiring a broader protocol redesign.
- Idea 2 would make the docs more honest, but it would also remove a capability that the wire format and clients already understand.
- Idea 4 is appealing but depends on broader `MSG_ACK`/`MSG_ERROR` semantics that are explicitly still thin.

## Chosen Hypothesis

If `server_handle_command()` turns `CMD_SPAWN_COLONY` into a minimal real world mutation for empty in-bounds coordinates, then the protocol surface becomes honest and server-side correctness tests can observe a new one-cell colony without breaking existing command serialization coverage.

## Experiment

- Added a narrow `server_spawn_colony()` helper in `src/server/server.c`.
- Spawn behavior now:
  - rounds `x`/`y` to grid coordinates
  - rejects out-of-bounds or occupied targets
  - creates a one-cell random-genome colony
  - uses provided command name when non-empty, otherwise generates a scientific name
  - updates tracked cell bookkeeping with `world_colony_add_cell()`
  - syncs the changed `World` back into `AtomicWorld`
- Replaced the old spawn log-only stub with success/rejection logging.
- Strengthened `tests/test_server_branch_coverage.c` to verify successful, blocked, and out-of-bounds spawn cases.
- Registered the server branch coverage binary in `tests/CMakeLists.txt` so it is part of CTest.

## Tests Run

1. Reconfigure and rebuild:

```bash
cmake -S . -B build && cmake --build build -j4
```

2. Focused correctness suite:

```bash
ctest --test-dir build --output-on-failure -R "ServerBranchCoverageTests|Phase4Tests|ProtocolEdgeTests"
```

3. Direct binary spot-check during debugging:

```bash
./build/tests/test_server_branch_coverage
```

## Results

- `Phase4Tests`: passed
- `ProtocolEdgeTests`: passed
- `ServerBranchCoverageTests`: passed
- Observed behavior now matches the documented command surface more closely: valid spawns create a real colony, while occupied or invalid coordinates are rejected without mutating the world.

## Interpretation

- The hypothesis was supported.
- This run did not target performance, so no repeated benchmark loop was needed.
- The change is small enough to keep: it improves protocol honesty/correctness and is validated by focused tests.

## Docs Updated

- `docs/PROTOCOL.md`
- `docs/ARCHITECTURE.md`
- `docs/TESTING.md`
- `docs/agent-handoffs/latest.md`
- `docs/lab-notes/20260324-173931-command-spawn-colony-honesty.md`

## Next Recommended Experiment

Build on the now-real manual spawn path by adding structured rejection feedback (`MSG_ERROR` or `MSG_ACK`) so clients can distinguish successful spawns from occupied/out-of-bounds requests without inferring it indirectly from later world snapshots.
