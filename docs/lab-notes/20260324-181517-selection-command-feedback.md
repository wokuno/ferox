# Lab Note 20260324-181517 selection-command-feedback

## Starting Context

- Run label: `20260324-181517-selection-command-feedback`
- Repository state reviewed with `git status --short --branch` before edits.
- Prior handoff reviewed at `docs/agent-handoffs/latest.md`.
- Prior notebooks reviewed:
  - `docs/lab-notes/20260324-165209-snapshot-colony-id-lookup.md`
  - `docs/lab-notes/20260324-171525-broadcast-inline-grid-allocation.md`
  - `docs/lab-notes/20260324-171905-one-pass-world-state-serialization.md`
  - `docs/lab-notes/20260324-172911-raw-grid-decode-research.md`
  - `docs/lab-notes/20260324-173931-command-spawn-colony-honesty.md`
  - `docs/lab-notes/20260324-175218-spawn-command-feedback.md`
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
  - `src/client/client.c`
  - `src/client/client.h`
  - `src/gui/gui_client.c`
  - `src/gui/gui_client.h`
  - `src/shared/protocol.h`
  - `src/shared/protocol.c`
  - `src/server/world.c`
  - `tests/test_server_branch_coverage.c`
  - `tests/test_phase6.c`
  - `tests/test_client_logic_surface.c`
  - `tests/test_protocol_edge.c`

## Prior Handoff Decision

- Accepted after fresh re-triage.
- The prior handoff recommended extending `ProtoCommandStatus` beyond spawn-only behavior.
- Fresh research still supported that direction because `CMD_SELECT_COLONY` remained a user-visible silent failure path: successful selection only implied success through later `MSG_COLONY_INFO`, and invalid selection produced no structured reply.

## Research Area Spread

- Protocol correctness and command semantics
- Client and GUI state synchronization
- Selection/detail UX and observability
- Simulation and colony lifecycle alignment
- Testing/tooling coverage
- Performance-scope screening and documentation maintenance

## Research Topics

1. Current worktree safety.
   - Takeaway: there are unrelated pre-existing user changes in root/docs/scripts files, so this run must avoid reverting them.
   - Sources: `git status --short --branch`

2. Latest handoff recommendation.
   - Takeaway: extending `ProtoCommandStatus` to another command family was suggested and remained a plausible next slice.
   - Sources: `docs/agent-handoffs/latest.md`

3. Recent notebook sequence.
   - Takeaway: recent keeps alternated between transport perf and command-surface correctness, so another narrow protocol-UX slice fits the current run pattern.
   - Sources: recent `docs/lab-notes/*.md`

4. Development-cycle constraints.
   - Takeaway: the next change should stay narrow, falsifiable, reversible, and documented with focused tests.
   - Sources: `docs/DEVELOPMENT_CYCLE.md`

5. Testing expectations for behavior/protocol work.
   - Takeaway: correctness-only validation is enough here; benchmarking is not appropriate for a selection-feedback slice.
   - Sources: `docs/TESTING.md`, `docs/PERF_RUNBOOK.md`

6. Architecture command contract.
   - Takeaway: selection is a first-class client command, so it should have immediate success/failure semantics just like spawn now does.
   - Sources: `docs/ARCHITECTURE.md`

7. Protocol spec after spawn feedback.
   - Takeaway: `MSG_ACK` / `MSG_ERROR` are now live, but docs only described spawn outcomes, leaving selection as the obvious next consumer.
   - Sources: `docs/PROTOCOL.md`

8. Shared command-status payload shape.
   - Takeaway: `ProtoCommandStatus` already includes command id, status code, entity id, and message, which is enough for selection accept/reject/clear without redesign.
   - Sources: `src/shared/protocol.h`, `src/shared/protocol.c`

9. Current server selection handling.
   - Takeaway: `CMD_SELECT_COLONY` currently just stores `selected_colony` and tries to send `MSG_COLONY_INFO`; it never returns structured success/failure feedback.
   - Sources: `src/server/server.c`

10. `server_send_colony_info()` missing-target behavior.
    - Takeaway: if the colony id is missing or inactive, the function still serializes a mostly empty detail payload instead of signaling selection failure explicitly.
    - Sources: `src/server/server.c`

11. World lookup semantics.
    - Takeaway: `world_get_colony()` already rejects inactive or missing colonies, making it a clean selection-validation gate.
    - Sources: `src/server/world.c`

12. Broadcast follow-up on selected colonies.
    - Takeaway: the server already refreshes selected-colony detail after each world broadcast when `client->selected_colony != 0`, so successful selection can keep the existing detail path.
    - Sources: `src/server/server.c`

13. Terminal client ACK/ERROR behavior.
    - Takeaway: the client currently stores command status but only uses selection errors to clear stale detail if the rejected id matches the local selection, leaving other cases ambiguous.
    - Sources: `src/client/client.c`

14. GUI client ACK/ERROR behavior.
    - Takeaway: the GUI client has the same ambiguity and can cheaply synchronize selected colony id from status payloads.
    - Sources: `src/gui/gui_client.c`

15. Command-status lifecycle in clients.
    - Takeaway: both clients already clear stale command status on deselect and world refresh for spawn-specific status, so selection can reuse that small-state pattern.
    - Sources: `src/client/client.c`, `src/gui/gui_client.c`

16. Terminal status-bar affordance.
    - Takeaway: the terminal renderer already displays command status text with success/error color coding, so selection feedback will surface automatically once messages exist.
    - Sources: `src/client/renderer.c`

17. GUI status-bar affordance.
    - Takeaway: the GUI renderer already displays command status text with the same success/error color split, so no new UI surface is needed.
    - Sources: `src/gui/gui_renderer.c`

18. Existing server branch coverage.
    - Takeaway: `tests/test_server_branch_coverage.c` already covers select and spawn branches and is the right place to verify selection ack/error behavior.
    - Sources: `tests/test_server_branch_coverage.c`

19. Existing phase-6 client coverage.
    - Takeaway: `tests/test_phase6.c` already verifies command-status handling and can be retargeted from spawn-only to selection semantics.
    - Sources: `tests/test_phase6.c`

20. Existing client surface test affordance.
    - Takeaway: `tests/test_client_logic_surface.c` stubs command-status deserialization already, so it can cheaply verify selection-state synchronization.
    - Sources: `tests/test_client_logic_surface.c`

21. Existing protocol edge coverage.
    - Takeaway: protocol tests already roundtrip `ProtoCommandStatus`; a select-clear status example would make the multi-command reuse more explicit.
    - Sources: `tests/test_protocol_edge.c`

22. Colony intelligence / explainability alignment.
    - Takeaway: explicit selection rejection helps keep the UI explainable rather than forcing users to infer missing state from absent follow-up details.
    - Sources: `docs/COLONY_INTELLIGENCE.md`

23. Simulation lifecycle relevance.
    - Takeaway: selection does not change simulation state, so adding immediate feedback improves observability without perturbing model behavior.
    - Sources: `docs/SIMULATION.md`

24. Genetics/model coupling risk.
    - Takeaway: selection feedback is model-neutral and lower risk than deeper ecology or genetics work in a one-cycle experiment.
    - Sources: `docs/GENETICS.md`

25. Scaling roadmap screening.
    - Takeaway: this slice does not interfere with large-world scaling work and is much smaller than another transport optimization cycle.
    - Sources: `docs/SCALING_AND_BEHAVIOR_PLAN.md`

26. Science benchmark screening.
    - Takeaway: no science benchmark is needed because this is a protocol/client correctness experiment rather than a model-behavior change.
    - Sources: `docs/SCIENCE_BENCHMARKS.md`

27. Code-review backlog relevance.
    - Takeaway: protocol honesty remains an active theme in the review log, and selection is the next obvious silent command surface.
    - Sources: `docs/CODEBASE_REVIEW_LOG.md`

28. Future experiment screening: reset feedback.
    - Takeaway: reset is a possible next target, but selection is lower risk because it can reuse existing `MSG_COLONY_INFO` follow-through with smaller state changes.
    - Sources: `src/server/server.c`, `docs/ARCHITECTURE.md`

## Candidate Experiment Ideas

1. Add structured `MSG_ACK` / `MSG_ERROR` feedback for `CMD_SELECT_COLONY`.
   - Pros: closes the clearest remaining silent command path with small scope.
   - Cons: needs careful client state synchronization.

2. Add structured feedback for `CMD_RESET`.
   - Pros: useful command honesty improvement.
   - Cons: reset success/failure has more world-replacement coupling than selection.

3. Generalize all remaining commands at once.
   - Pros: cleaner long-term surface.
   - Cons: too broad for one reversible run.

4. Return to raw-grid encode optimization.
   - Pros: still relevant for transport perf.
   - Cons: keeps deferring an obvious correctness/UX gap.

5. Promote `test_client_logic_surface` into CMake/CTest.
   - Pros: useful tooling cleanup.
   - Cons: less user-visible impact than fixing selection silence.

## Triage Reasoning

- Idea 1 wins on impact-to-effort ratio: it reuses the new status payload, improves immediate client feedback, and requires only a narrow server/client/test slice.
- Idea 2 is attractive later, but reset changes more live state and is a slightly riskier first generalization target.
- Idea 3 is the right long-term direction but too broad for a one-cycle falsifiable experiment.
- Idea 4 remains valid later, but correctness and observability still offer the best narrow keep.

## Chosen Hypothesis

If `CMD_SELECT_COLONY` reuses `ProtoCommandStatus` over `MSG_ACK` / `MSG_ERROR`, then clients and focused correctness tests can distinguish accepted selection, clear-selection, and missing-target rejection immediately without relying on absent `MSG_COLONY_INFO` side effects.

## Experiment

- Added a narrow `server_select_colony()` helper in `src/server/server.c`.
- Selection behavior now:
  - returns `MSG_ACK` with `PROTO_COMMAND_STATUS_ACCEPTED` and selected colony id when the colony exists and is active
  - returns `MSG_ACK` with `entity_id = 0` for explicit selection clear (`colony_id = 0`)
  - returns `MSG_ERROR` with `PROTO_COMMAND_STATUS_REJECTED` when the requested colony is missing or inactive
  - still sends `MSG_COLONY_INFO` after a successful non-zero selection
- Updated terminal and GUI clients to synchronize local selection state from selection-status ACK/ERROR messages and clear stale detail state on rejection.
- Added/updated focused coverage in:
  - `tests/test_server_branch_coverage.c`
  - `tests/test_phase6.c`
  - `tests/test_client_logic_surface.c`
  - `tests/test_protocol_edge.c`

## Tests Run

1. Build focused targets:

```bash
cmake --build build -j4 --target test_protocol_edge test_server_branch_coverage test_phase6
clang -std=c11 -I. -Isrc -Isrc/client -Isrc/shared -Isrc/server -o build/tests/test_client_logic_surface tests/test_client_logic_surface.c
```

2. Focused test binaries during iteration:

```bash
./build/tests/test_protocol_edge
./build/tests/test_server_branch_coverage
./build/tests/test_phase6
./build/tests/test_client_logic_surface
```

3. Final focused correctness suite:

```bash
ctest --test-dir build --output-on-failure -R "ProtocolEdgeTests|ServerBranchCoverageTests|Phase6Tests"
./build/tests/test_client_logic_surface
```

4. Full rebuild sanity check:

```bash
cmake --build build -j4
```

## Benchmark Results

- Not run.
- Reason: this experiment changes protocol correctness and client selection feedback semantics, not a throughput-sensitive hotspot.

## Results

- `ProtocolEdgeTests`: passed
- `ServerBranchCoverageTests`: passed
- `Phase6Tests`: passed
- `test_client_logic_surface`: passed
- Full rebuild completed successfully after the kept change.

## Interpretation

- The hypothesis was supported.
- Selection now behaves like spawn in the command-feedback surface: success, clear, and rejection are all explicit and immediate.
- This is a keep because it improves protocol honesty and client observability with a small, reusable extension of the existing status pattern.

## Docs Updated

- `docs/PROTOCOL.md`
- `docs/ARCHITECTURE.md`
- `docs/TESTING.md`
- `docs/PERFORMANCE_RESEARCH.md`
- `docs/agent-handoffs/latest.md`
- `docs/lab-notes/20260324-181517-selection-command-feedback.md`

## Next Recommended Experiment

Extend the same command-status surface to one more command family such as `CMD_RESET` or speed-limit clamping so remaining command outcomes stop depending on optimistic client state or later snapshots.
