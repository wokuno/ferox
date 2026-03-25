# Lab Note 20260324-184054 reset-command-feedback

## Starting Context

- Run label: `20260324-184054-reset-command-feedback`
- Repository state reviewed with `git status --short --branch` before edits.
- Prior handoff reviewed at `docs/agent-handoffs/latest.md`.
- Prior notebooks reviewed under `docs/lab-notes/`, especially the recent command-status and transport runs from `20260324`.
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
  - `docs/AUTONOMOUS_RESEARCH_LOOP.md`
  - `docs/FEROX_CLUB.md`
- Subsystem code/tests reviewed:
  - `src/server/server.c`
  - `src/server/server.h`
  - `src/shared/protocol.h`
  - `src/shared/protocol.c`
  - `src/client/client.c`
  - `src/client/client.h`
  - `src/gui/gui_client.c`
  - `src/gui/gui_client.h`
  - `tests/test_server_branch_coverage.c`
  - `tests/test_phase5.c`
  - `tests/test_phase6.c`
  - `tests/test_client_logic_surface.c`
  - `tests/test_protocol_edge.c`
  - `tests/CMakeLists.txt`

## Prior Handoff Decision

- Accepted after fresh triage.
- The prior handoff suggested extending structured command feedback to another command family such as `CMD_RESET` or speed-limit clamping.
- Fresh research kept `CMD_RESET` on top because it mutates the whole world, can invalidate selection/detail state immediately, and still had no structured success/failure surface.

## Research Area Spread

- Protocol correctness and command semantics
- Client and GUI UX / observability
- Simulation state lifecycle and reset behavior
- Testing and tooling hygiene
- Performance-scope screening
- Local dashboard/site and documentation maintenance

## Research Topics

1. Current worktree safety.
   - Takeaway: unrelated user edits still exist in root/docs/scripts areas, so this run must touch only experiment files.
   - Sources: `git status --short --branch`

2. Prior handoff recommendation.
   - Takeaway: `CMD_RESET` or speed-limit feedback were the best advisory next steps, but not mandatory.
   - Sources: `docs/agent-handoffs/latest.md`

3. Recent notebook sequence.
   - Takeaway: recent command-surface keeps already covered spawn and selection, so reset is the next obvious silence gap.
   - Sources: recent `docs/lab-notes/*.md`

4. Development-cycle constraints.
   - Takeaway: one narrow reversible experiment with matching docs/tests remains the right shape.
   - Sources: `docs/DEVELOPMENT_CYCLE.md`

5. Testing expectations.
   - Takeaway: correctness-only validation is appropriate here; repeated benchmarking is not required for this slice.
   - Sources: `docs/TESTING.md`, `docs/PERF_RUNBOOK.md`

6. Architecture contract for commands.
   - Takeaway: reset is already documented as a first-class client command, so it should expose explicit outcomes like spawn and selection now do.
   - Sources: `docs/ARCHITECTURE.md`

7. Protocol spec status.
   - Takeaway: `MSG_ACK` / `MSG_ERROR` already document spawn and selection usage but not reset, leaving docs/behavior parity incomplete.
   - Sources: `docs/PROTOCOL.md`

8. Shared command-status payload reuse.
   - Takeaway: `ProtoCommandStatus` already has enough fields for reset with no wire redesign.
   - Sources: `src/shared/protocol.h`, `src/shared/protocol.c`

9. Current reset implementation shape.
   - Takeaway: `CMD_RESET` rebuilds the world and atomic wrapper but only logs failure/success; it sends no structured reply.
   - Sources: `src/server/server.c`

10. Reset failure path semantics.
    - Takeaway: reset can genuinely fail if `world_create()` or `atomic_world_create()` fails, so explicit `MSG_ERROR` has real value.
    - Sources: `src/server/server.c`

11. Reset-side selection invalidation.
    - Takeaway: successful reset replaces all colonies, so any pre-reset `selected_colony` value becomes stale immediately.
    - Sources: `src/server/server.c`, `src/server/server.h`

12. Broadcast follow-up after reset.
    - Takeaway: the next snapshot eventually corrects client state, but that is delayed and implicit rather than immediate.
    - Sources: `src/server/server.c`

13. Colony info send behavior.
    - Takeaway: `server_send_colony_info()` can send an empty-ish detail payload when a colony is missing, so clearing selection proactively after reset is safer.
    - Sources: `src/server/server.c`

14. Terminal client status handling.
    - Takeaway: terminal client already consumes `MSG_ACK` / `MSG_ERROR`, so reset can reuse existing UI affordances cheaply.
    - Sources: `src/client/client.c`

15. GUI client status handling.
    - Takeaway: GUI client mirrors the same pattern and can clear renderer selection immediately on reset acceptance.
    - Sources: `src/gui/gui_client.c`

16. Existing optimistic local state for pause/speed.
    - Takeaway: speed and pause commands still rely on optimistic local updates, but reset is lower-risk to generalize first because it has no local “preview” state worth preserving.
    - Sources: `src/client/client.c`, `src/gui/gui_client.c`

17. Existing test coverage for reset.
    - Takeaway: branch coverage checks world replacement, but not `MSG_ACK` semantics or selection clearing.
    - Sources: `tests/test_server_branch_coverage.c`

18. Existing client correctness coverage.
    - Takeaway: `Phase6Tests` and `test_client_logic_surface` already verify command-status handling and can extend to reset cheaply.
    - Sources: `tests/test_phase6.c`, `tests/test_client_logic_surface.c`

19. Existing protocol edge coverage.
    - Takeaway: protocol edge tests already roundtrip `ProtoCommandStatus`, so reset can become another explicit payload example.
    - Sources: `tests/test_protocol_edge.c`

20. Legacy phase-5 assumption.
    - Takeaway: `tests/test_phase5.c` still assumed selecting a missing colony sets the id optimistically, which no longer matches the newer command-status behavior.
    - Sources: `tests/test_phase5.c`

21. Simulation correctness screening.
    - Takeaway: reset feedback changes observability and client state sync, not science/model behavior.
    - Sources: `docs/SIMULATION.md`, `docs/SCIENCE_BENCHMARKS.md`

22. Colony intelligence / explainability screening.
    - Takeaway: explicit reset acknowledgment aligns with the project’s explainability goal by making state changes legible instead of inferred.
    - Sources: `docs/COLONY_INTELLIGENCE.md`

23. Genetics screening.
    - Takeaway: reset creates a fresh random colony population, but this run does not change any genome/model rules.
    - Sources: `docs/GENETICS.md`

24. Scaling roadmap screening.
    - Takeaway: this command-surface fix is orthogonal to large-map scaling and low risk to keep.
    - Sources: `docs/SCALING_AND_BEHAVIOR_PLAN.md`

25. Local dashboard/site screening.
    - Takeaway: `ferox.club` already surfaces latest summaries and notebooks; no dashboard code change is needed for this narrow command experiment.
    - Sources: `docs/FEROX_CLUB.md`, `cmd/feroxclub/main.go`

26. Autonomous loop workflow screening.
    - Takeaway: this run still needs notebook, handoff, docs, tests, and a commit if kept.
    - Sources: `docs/AUTONOMOUS_RESEARCH_LOOP.md`

27. External acknowledgement semantics guidance.
    - Takeaway: application-level acknowledgements are what indicate a peer acted on a request, not just transport delivery.
    - Sources: RabbitMQ reliability guide <https://www.rabbitmq.com/docs/reliability>

28. External protocol semantics guidance.
    - Takeaway: explicit request/response semantics are preferable to relying on later indirect observable state changes.
    - Sources: RFC 9110 <https://datatracker.ietf.org/doc/html/rfc9110>

29. External testing-shape guidance.
    - Takeaway: this change fits the test-pyramid guidance well because it is covered mostly by narrow protocol/unit/integration-style tests instead of broad end-to-end runs.
    - Sources: Fowler/Vocke test-pyramid article <https://martinfowler.com/articles/practical-test-pyramid.html>

30. Performance screening.
    - Takeaway: raw-grid encode and other transport work remain viable later, but this run’s best measurable improvement is correctness/UX rather than throughput.
    - Sources: `docs/PERFORMANCE.md`, `docs/PERFORMANCE_RESEARCH.md`

## Candidate Experiment Ideas

1. Add structured `MSG_ACK` / `MSG_ERROR` feedback for `CMD_RESET`.
   - Pros: closes the clearest remaining silent state-changing command gap.
   - Cons: touches server and both clients.

2. Add structured feedback for speed-limit clamping.
   - Pros: could reduce optimistic local-state drift.
   - Cons: more UI/local-state coupling and less severe than reset.

3. Generalize feedback for pause/resume too.
   - Pros: cleaner full command surface.
   - Cons: too broad for one reversible cycle.

4. Promote `test_client_logic_surface` into CTest.
   - Pros: useful tooling cleanup.
   - Cons: lower user-facing impact than reset semantics.

5. Return to raw-grid encode optimization.
   - Pros: still promising for transport throughput.
   - Cons: keeps deferring a visible command-state correctness gap.

## Triage Reasoning

- Idea 1 won because reset mutates the entire world, invalidates local selection state, and still had no explicit success/failure semantics.
- Idea 2 is a good next follow-up, but speed-limit feedback is less critical than reset because the world is not rebuilt and stale colony-detail state is not involved.
- Idea 3 is the long-term direction, but doing all remaining commands in one pass would be broader than the current cycle allows.
- Ideas 4 and 5 remain useful later but were lower confidence for this run’s best impact-to-effort ratio.

## Chosen Hypothesis

If `CMD_RESET` reuses `ProtoCommandStatus` over `MSG_ACK` / `MSG_ERROR`, then clients and focused correctness tests can distinguish accepted versus failed world resets immediately and clear stale local selection/detail state on success instead of relying on later snapshot side effects.

## Experiment

- Added `server_reset_world()` in `src/server/server.c` to wrap the existing rebuild logic and emit structured command status.
- Added `server_clear_selected_colonies()` so accepted resets clear stale per-client `selected_colony` values immediately after world replacement.
- Updated `server_handle_command()` so `CMD_RESET` now sends:
  - `MSG_ACK` with `PROTO_COMMAND_STATUS_ACCEPTED` and `entity_id = 0` on success
  - `MSG_ERROR` with `PROTO_COMMAND_STATUS_INTERNAL_ERROR` on rebuild failure
- Updated terminal and GUI clients so reset acceptance clears local selection/detail state immediately from the command-status message.
- Updated focused coverage in:
  - `tests/test_server_branch_coverage.c`
  - `tests/test_phase5.c`
  - `tests/test_phase6.c`
  - `tests/test_client_logic_surface.c`
  - `tests/test_protocol_edge.c`

## Tests Run

1. Build focused targets:

```bash
cmake --build build -j4 --target test_protocol_edge test_server_branch_coverage test_phase5 test_phase6 && clang -std=c11 -I. -Isrc -Isrc/client -Isrc/shared -Isrc/server -o build/tests/test_client_logic_surface tests/test_client_logic_surface.c
```

2. Focused correctness suite:

```bash
ctest --test-dir build --output-on-failure -R "ProtocolEdgeTests|ServerBranchCoverageTests|Phase5Tests|Phase6Tests"
./build/tests/test_client_logic_surface
```

## Benchmark Results

- Not run.
- Reason: this experiment changes protocol correctness and client observability semantics, not a throughput-sensitive hotspot. Repeated benchmarking was not appropriate for this slice.

## Results

- `ProtocolEdgeTests`: passed
- `ServerBranchCoverageTests`: passed
- `Phase5Tests`: passed
- `Phase6Tests`: passed
- `test_client_logic_surface`: passed

## Interpretation

- The hypothesis was supported.
- `CMD_RESET` now behaves like spawn and selection in the command-feedback surface: success/failure is explicit, and accepted resets clear stale local colony selection/detail state immediately.
- This is a keep. It improves protocol honesty and client-state correctness with a small reusable extension of the existing status pattern.

## Docs Updated

- `docs/PROTOCOL.md`
- `docs/ARCHITECTURE.md`
- `docs/TESTING.md`
- `docs/PERFORMANCE_RESEARCH.md`
- `docs/agent-handoffs/latest.md`
- `docs/lab-notes/20260324-184054-reset-command-feedback.md`

## Next Recommended Experiment

Extend the same command-status surface to speed-limit clamping or pause/resume so optimistic local playback state can be reconciled immediately instead of waiting for later world snapshots.
