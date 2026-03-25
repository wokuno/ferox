# Lab Note 20260324-175218 spawn-command-feedback

## Starting Context

- Run label: `20260324-175218-spawn-command-feedback`
- Repository state reviewed with `git status --short --branch` before edits.
- Prior handoff reviewed at `docs/agent-handoffs/latest.md`.
- Prior notebooks reviewed:
  - `docs/lab-notes/20260324-165209-snapshot-colony-id-lookup.md`
  - `docs/lab-notes/20260324-171525-broadcast-inline-grid-allocation.md`
  - `docs/lab-notes/20260324-171905-one-pass-world-state-serialization.md`
  - `docs/lab-notes/20260324-172911-raw-grid-decode-research.md`
  - `docs/lab-notes/20260324-173931-command-spawn-colony-honesty.md`
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
  - `src/shared/protocol.h`
  - `src/shared/protocol.c`
  - `src/client/client.c`
  - `src/client/client.h`
  - `src/client/renderer.c`
  - `src/client/main.c`
  - `src/gui/gui_client.c`
  - `src/gui/gui_client.h`
  - `src/gui/gui_renderer.c`
  - `src/gui/gui_renderer.h`
  - `tests/test_server_branch_coverage.c`
  - `tests/test_protocol_edge.c`
  - `tests/test_phase6.c`
  - `tests/test_client_logic_surface.c`

## Prior Handoff Decision

- Accepted in spirit after fresh research.
- The prior handoff recommended structured spawn feedback, but this run still re-triangulated other options before choosing it.
- Fresh research kept it on top because the newly honest spawn command still lacked immediate client-visible outcome semantics, making protocol honesty incomplete.

## Research Area Spread

- Protocol correctness and transport semantics
- Client/GUI UX and observability
- Simulation behavior / colony lifecycle alignment
- Testing/tooling coverage
- Performance and implementation-risk screening
- Documentation / plan maintenance

## Research Topics

1. Current worktree safety.
   - Takeaway: there are unrelated pre-existing root/docs/script edits, so this run must avoid reverting them.
   - Sources: `git status --short --branch`

2. Latest handoff recommendation.
   - Takeaway: structured spawn feedback was suggested, but it was advisory and needed fresh comparison against other candidates.
   - Sources: `docs/agent-handoffs/latest.md`

3. Recent autonomous run pattern.
   - Takeaway: the last several keeps split between transport perf and spawn correctness, so protocol-surface follow-through is justified if it closes a visible gap.
   - Sources: recent lab notes under `docs/lab-notes/`

4. Development-cycle guidance.
   - Takeaway: the best next slice should stay narrow, reversible, documented, and paired with targeted tests.
   - Sources: `docs/DEVELOPMENT_CYCLE.md`

5. Testing expectations for behavior work.
   - Takeaway: correctness-only validation is enough here; no benchmark loop is required for a protocol-feedback slice.
   - Sources: `docs/TESTING.md`, `docs/PERF_RUNBOOK.md`

6. Architecture contract for commands.
   - Takeaway: clients already send manual spawn requests as first-class commands, so they should not need to infer acceptance indirectly.
   - Sources: `docs/ARCHITECTURE.md`

7. Protocol spec gap after the last run.
   - Takeaway: `CMD_SPAWN_COLONY` is now real, but `MSG_ACK` and `MSG_ERROR` were still documented as inert.
   - Sources: `docs/PROTOCOL.md`

8. Shared protocol enums.
   - Takeaway: `MSG_ACK` and `MSG_ERROR` already exist in the wire enum, so a narrow payload addition does not require a new message type.
   - Sources: `src/shared/protocol.h`

9. Command serialization surface.
   - Takeaway: spawn command payload is stable already; the missing piece is response serialization, not request shape.
   - Sources: `src/shared/protocol.c`

10. Server spawn implementation.
    - Takeaway: `server_spawn_colony()` already distinguishes success, conflict, and out-of-bounds locally, so it can produce structured statuses with little extra branching.
    - Sources: `src/server/server.c`

11. Client CLI message handling.
    - Takeaway: terminal client currently ignores `MSG_ACK` and `MSG_ERROR`, but already has a status bar that can surface short messages cheaply.
    - Sources: `src/client/client.c`, `src/client/renderer.c`

12. GUI message handling.
    - Takeaway: GUI client also ignores `MSG_ACK` and `MSG_ERROR`, but its status bar already has space for short live telemetry text.
    - Sources: `src/gui/gui_client.c`, `src/gui/gui_renderer.c`

13. Client selection/detail invalidation rules.
    - Takeaway: both clients already clear stale selected-detail state on world updates and deselection, so command-status state can reuse the same lightweight lifecycle.
    - Sources: `src/client/client.c`, `src/gui/gui_client.c`

14. Existing protocol edge coverage.
    - Takeaway: command serialization tests cover request payloads but not any structured response payload.
    - Sources: `tests/test_protocol_edge.c`

15. Existing server branch coverage.
    - Takeaway: spawn branch tests now verify world mutation outcomes and are a good place to assert concrete ack/error semantics too.
    - Sources: `tests/test_server_branch_coverage.c`

16. Existing client behavior coverage.
    - Takeaway: terminal client surface tests already dispatch `MSG_ACK` and `MSG_ERROR`, but only as no-op branches.
    - Sources: `tests/test_client_logic_surface.c`

17. Existing phase-6 client tests.
    - Takeaway: phase-6 already validates colony-detail deserialization, so adding command-status decoding there is a natural fit.
    - Sources: `tests/test_phase6.c`

18. Simulation lifecycle alignment.
    - Takeaway: manual spawn is explicitly part of the colony lifecycle docs, so immediate feedback strengthens model transparency rather than changing behavior.
    - Sources: `docs/SIMULATION.md`

19. Colony intelligence / explainability angle.
    - Takeaway: richer client messaging improves observability and keeps the interface aligned with Ferox's explainability goals.
    - Sources: `docs/COLONY_INTELLIGENCE.md`

20. Genetics/model coupling.
    - Takeaway: the minimal spawn path still uses random genomes and generated names when needed, so feedback should report acceptance state only, not over-promise biological guarantees.
    - Sources: `docs/GENETICS.md`, `src/server/server.c`

21. Scaling/behavior roadmap screening.
    - Takeaway: this slice does not block larger-map scaling work and is smaller than returning to another transport micro-optimization immediately.
    - Sources: `docs/SCALING_AND_BEHAVIOR_PLAN.md`

22. Science benchmark screening.
    - Takeaway: science scenarios remain important, but this run can deliver a clearer correctness win with less setup cost.
    - Sources: `docs/SCIENCE_BENCHMARKS.md`

23. Code-review backlog relevance.
    - Takeaway: protocol honesty was already a review finding, and immediate accept/reject semantics complete that thread more fully.
    - Sources: `docs/CODEBASE_REVIEW_LOG.md`

24. External ack semantics guidance.
    - Takeaway: transport reliability alone does not mean the peer application acted on a message; application-level acknowledgement is the right tool for accepted-vs-rejected command semantics.
    - Sources: RFC 6587 section 3.2/3.4 contrast, RabbitMQ reliability guide

25. External byte-order / payload safety guidance.
    - Takeaway: any new payload should stay explicit, fixed-width, and endian-safe using the existing `hton*`/`ntoh*` helpers and memcpy-based field copies.
    - Sources: `byteorder(3)`, `memcpy` cppreference, `src/shared/protocol.c`

26. Performance-side screening.
    - Takeaway: raw-grid encode remains a good future perf candidate, but spawn feedback is a lower-effort correctness win and benchmarking is not the right gate for this run.
    - Sources: `docs/PERFORMANCE_RESEARCH.md`, `tests/test_perf_unit_protocol.c`

27. Cache/stencil external screening.
    - Takeaway: RRZE layer-condition research is still relevant for future field-update work, but not the best immediate experiment given the active spawn semantics gap.
    - Sources: RRZE layer-condition notes, `docs/PERFORMANCE_RESEARCH.md`

## Candidate Experiment Ideas

1. Add structured `MSG_ACK` / `MSG_ERROR` payloads for `CMD_SPAWN_COLONY`.
   - Pros: closes the most obvious remaining honesty gap with narrow scope.
   - Cons: touches shared protocol plus both clients.

2. Generalize structured feedback for all commands at once.
   - Pros: cleaner long-term surface.
   - Cons: too broad for one reversible cycle.

3. Return only server logs plus stronger docs.
   - Pros: almost zero code.
   - Cons: leaves clients blind.

4. Return to raw-grid encode micro-optimization.
   - Pros: still promising for transport throughput.
   - Cons: keeps deferring visible protocol correctness follow-through.

5. Add richer spawn-placement heuristics.
   - Pros: deeper behavior feature.
   - Cons: larger model-scope change with weak reversibility.

## Triage Reasoning

- Idea 1 wins on impact-to-effort ratio: it finishes the documented spawn contract without opening a bigger protocol redesign.
- Idea 2 is attractive long term, but this run only needs one falsifiable slice.
- Idea 4 remains valid later, but fresh research says protocol correctness is the highest-confidence keep right now.

## Chosen Hypothesis

If accepted and rejected `CMD_SPAWN_COLONY` outcomes are serialized into a small shared `ProtoCommandStatus` payload and sent via `MSG_ACK` / `MSG_ERROR`, then both clients and focused correctness tests can distinguish spawn success, occupancy conflict, and out-of-bounds rejection immediately without waiting for a later world snapshot.

## Experiment

- Added a fixed-width `ProtoCommandStatus` payload plus serializer/deserializer in `src/shared/protocol.h` and `src/shared/protocol.c`.
- Updated `src/server/server.c` so `CMD_SPAWN_COLONY` now emits:
  - `MSG_ACK` with `PROTO_COMMAND_STATUS_ACCEPTED` and spawned colony id on success
  - `MSG_ERROR` with `PROTO_COMMAND_STATUS_CONFLICT` or `PROTO_COMMAND_STATUS_OUT_OF_BOUNDS` on rejection
- Updated terminal and GUI clients to deserialize command-status messages, store the latest short status, and surface it in the status bar.
- Kept status lifecycle narrow by clearing stale spawn status on world refresh/deselect paths.
- Added focused coverage in:
  - `tests/test_protocol_edge.c`
  - `tests/test_server_branch_coverage.c`
  - `tests/test_phase6.c`
  - `tests/test_client_logic_surface.c`

## Tests Run

1. Build targeted binaries:

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
- Reason: this experiment changes protocol correctness and client feedback semantics, not a throughput-sensitive hotspot. Per the run contract, benchmarking was not appropriate for this slice.

## Results

- `ProtocolEdgeTests`: passed
- `ServerBranchCoverageTests`: passed
- `Phase6Tests`: passed
- `test_client_logic_surface`: passed
- Full rebuild completed successfully after the kept change.

## Interpretation

- The hypothesis was supported.
- The server now gives immediate structured spawn outcomes, and both clients can consume them.
- This is a keep: it improves protocol honesty and observability with a small reversible payload addition and focused tests.

## Docs Updated

- `docs/PROTOCOL.md`
- `docs/ARCHITECTURE.md`
- `docs/TESTING.md`
- `docs/PERFORMANCE_RESEARCH.md`
- `docs/agent-handoffs/latest.md`
- `docs/lab-notes/20260324-175218-spawn-command-feedback.md`

## Next Recommended Experiment

Generalize structured command feedback beyond manual spawn so selection/reset and future command failures can use the same small status payload instead of silent no-op behavior.
