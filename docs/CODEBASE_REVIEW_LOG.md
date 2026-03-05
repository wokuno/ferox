# Codebase Review Log

Date: 2026-03-05

## Scope

Requested audit:
- Review the codebase for correctness, maintainability, and design risks.
- Run the test suite and record results.
- Evaluate runtime performance and identify bottlenecks.
- Research improvement directions and produce a concrete execution plan.
- Update documentation between phases.

## Working Method

Phases:
1. Context and audit setup
2. Baseline test execution
3. Performance evaluation
4. Code review and research
5. Improvement plan

Documentation rule:
- This file is updated after each phase with results, blockers, and next actions.

## Phase Updates

### Phase 1: Context and audit setup

Status: Complete

Notes:
- Repository structure identified as a C/CMake project with server, shared, terminal client, GUI client, and a large test suite.
- Existing local modifications were detected in `docs/PERFORMANCE.md`, `docs/RUNNER_OPS.md`, plus untracked perf artifacts and `scripts/perf_scenarios.py`.
- To avoid conflicts, audit notes are isolated in this file.

Next:
- Inspect build, test, and performance scripts plus architecture docs before running anything.

Results:
- `scripts/test.sh` uses the `build/` directory, defers to CTest, and has separate categories for general, stress, and perf runs.
- `tests/CMakeLists.txt` defines 19 named CTest targets including heavy stress, visual, SIMD, and performance suites.
- `scripts/perf_scenarios.py` already supports repeated scenario runs and bottleneck summaries across build types and scales.
- Architecture docs already note some likely review targets: full-state world broadcasts, incomplete command handling for `CMD_SPAWN_COLONY`, and atomic-path overheads.

Next:
- Rebuild if needed, list tests from the active build, then execute the baseline suite and capture failures or instability.

### Phase 2: Baseline test execution

Status: Complete

Results:
- Rebuilt the active `build/` tree successfully with no compile failures.
- Initial sandboxed run reported 18/20 passing when excluding `AllTests`; the two failures were `Phase4Tests` and `Phase5Tests`.
- Failure pattern was environmental, not functional: every failing assertion occurred at the first local server/socket creation call.
- Confirmed by rerunning outside the sandbox:
  - `Phase4Tests`: pass
  - `Phase5Tests`: pass
  - full `ctest --output-on-failure -j 4`: **21/21 passed**
- End-to-end runtime for the full matrix was about **8.2s**, with `VisualStabilityTests` and `AllTests` the longest individual targets.

Notes:
- For this repository, local networking tests need unsandboxed execution to be representative.
- Logic, protocol, simulation, GUI-logic, stress, and performance-eval suites all passed in the baseline.

Next:
- Run fresh performance scenarios, compare debug and release behavior, and identify hotspots that deserve profiling or redesign.

### Phase 3: Performance evaluation

Status: Complete

Environment:
- Active baseline build type: `Release`
- Fresh scenario runs executed with `scripts/perf_scenarios.py` for:
  - `Debug`, scales `1 2`, repeats `2`
  - `Release`, scales `1 2`, repeats `2`

Measured results:
- Release summary:
  - slowest medians were `atomic_tick (1 thread)`, `atomic_tick (2 threads)`, `atomic_tick (4 threads)`, then serial simulation
  - average atomic/serial ratio: **1.52x**
  - tiny/batched threadpool ratio: **4.07x**
- Debug summary:
  - `debug_scale2` medians:
    - `atomic_tick (1 thread)`: **2715.78 ms**
    - `atomic_tick (2 threads)`: **1658.02 ms**
    - `atomic_tick (4 threads)`: **1379.82 ms**
    - `simulation_tick (serial)`: **1232.58 ms**
  - average atomic/serial ratio: **1.64x**
  - tiny/batched threadpool ratio: **4.96x**

Interpretation:
- The dominant bottleneck is structural, not compiler-optimization-sensitive: the atomic path remains slower than the serial path in both debug and release.
- The threadpool is efficient enough for coarse work, but expensive for tiny tasks; submission granularity is a real limiter.
- Protocol serialization is not the primary hotspot in current local runs.

Hot-path code correlations:
- `atomic_tick()` pays two barriers and two full-world synchronization passes each tick (`atomic_world_sync_to_world`, `atomic_world_sync_from_world`).
- Region tasks are finer than the scheduler likes for this workload.
- Serial follow-up passes still include full-grid work such as scents, combat, turnover, divisions, and recombinations.

Next:
- Convert the measured hotspots into concrete review findings and improvement tracks, then back those tracks with targeted external references.

### Phase 4: Code review and research

Status: Complete

Key findings under review:
- Atomic RNG seed assignment is based on task index instead of worker identity, which can race when multiple region tasks share the same `thread_id`.
- Scent source propagation appears to keep the first writer instead of the strongest contributor during diffusion.
- Server broadcasts hold the client mutex while sending to sockets, which can let one slow client stall unrelated work.
- `CMD_SPAWN_COLONY` is part of the protocol surface but is not implemented in the server.

External references consulted:
- RFC 3284 (VCDIFF) for portable delta encoding and target-window differencing.
- Clang Users Manual for instrumentation-based PGO.
- Clang ThinLTO documentation for parallel whole-program optimization and incremental cache support.
- OpenCilk work-stealing and Cilkscale docs for processor-oblivious scheduling and work/span-guided scalability analysis.

How the research maps to the repo:
- Delta world-state transport is a standards-backed option instead of inventing an ad hoc binary diff format.
- PGO and ThinLTO are low-risk build-system improvements before intrusive code surgery.
- Work/span style analysis is a better fit for the atomic path than raw wall-clock timing alone because it separates available parallelism from scheduler overhead.

Next:
- Produce a prioritized implementation plan that separates immediate fixes, near-term performance work, and larger architectural bets.

### Phase 5: Improvement plan

Status: Complete

Priority tiers:
1. Correctness and protocol honesty
2. Low-risk throughput wins
3. Parallel-runtime redesign
4. Network/state-distribution redesign

Immediate plan outline:
- Fix correctness bugs first: RNG-seed race, scent-source tracking, protocol/command mismatch.
- Add cheaper measurement loops next: perf CI artifacts, profiler-ready build flags, and a repeatable perf target matrix.
- Rework the scheduler/granularity problem before attempting deeper SIMD or lock-free changes.
- Prototype delta snapshots only after the world-update semantics are stabilized.

## Implementation Workflow

Status: In progress

Execution model:
- Create one GitHub issue per workstream so progress is externally visible.
- Use separate git worktrees and branches per issue to keep fixes isolated.
- Update this document and the matching GitHub issue after each meaningful checkpoint.
- Require each workstream to finish with:
  - code changes
  - tests added or updated when behavior changes
  - documentation updates
  - a pull request linked back to the issue

Planned workstreams:
1. Atomic RNG correctness and deterministic parallelism
2. Scent source propagation correctness
3. Broadcast path and protocol honesty (`CMD_SPAWN_COLONY`, delta/fullsnapshot follow-up)
4. Parallel runtime/performance improvements (task granularity, measurement hooks)

Current blocker:
- Cleared: GitHub auth was refreshed and issue/PR automation can proceed.

Issue mapping:
- `#73` Atomic RNG correctness and deterministic parallelism
- `#74` Scent source propagation correctness
- `#75` Broadcast contention and protocol-surface alignment
- `#76` Atomic-path performance improvements and instrumentation

Planned branch/worktree mapping:
- `issue-73-atomic-rng` -> `../ferox-wt-73`
- `issue-74-scent-source` -> `../ferox-wt-74`
- `issue-75-server-protocol` -> `../ferox-wt-75`
- `issue-76-atomic-perf` -> `../ferox-wt-76`

PR mapping:
- `#73` -> PR `#77`
- `#74` -> PR `#80`
- `#75` -> PR `#79`
- `#76` -> PR `#78`

Verification summary:
- `#73`: `SimulationLogicTests`, `VisualStabilityTests` passed in its worktree build.
- `#74`: `SimulationLogicTests`, `SimdEvalTests` passed in its worktree build.
- `#75`: `Phase4Tests`, `Phase5Tests` passed in its worktree build.
- `#76`: `PerformanceEvalTests` passed in its release worktree build; 4-thread atomic metric improved from `94.28 ms` to `89.30 ms` in the targeted release comparison.

## Validation And Merge

Status: In progress

Process:
- Run the full local CTest matrix against each PR branch in its worktree.
- Use representative unsandboxed execution for branches that exercise local sockets.
- Merge only the PRs that pass the full suite locally.

Current state:
- Full local suite passed for PRs `#77`, `#80`, `#79`, and `#78`.
- GitHub reports all four PRs as `CONFLICTING`, so they need to be restacked onto `origin/main` before merge.

Restack and merge outcome:
- PR `#79` merged after rebasing onto `origin/main`.
- PR `#78` merged after rebasing onto `origin/main`.
- PR `#80` merged after rebasing onto `origin/main`.
- PR `#77` remained stale and conflicting after the restack because `origin/main` already contained a newer deterministic atomic-RNG implementation; it should be closed instead of merged.

Final verification:
- Current `main` passed `ctest --output-on-failure -j 4` with `21/21` tests green on March 5, 2026.
- That run includes the suites called out by open issue `#70`: `Phase2Tests`, `GeneticsAdvancedTests`, and `CombatSystemTests`.

Final issue state plan:
- Close issue `#73` manually, because the scent-source fix landed via merged PR `#80` but the issue was not auto-closed.
- Close issue `#70` manually, because the failing assertions described there are now passing on current `main`.
- Close PR `#77` manually as superseded by newer upstream atomic RNG changes already present on `main`.

## Protected Main Recovery

Status: In progress

Progress:
- PR `#81` reverted the accidental direct push to protected `main` without rewriting history.
- `reapply/perf-eval-coverage-summary` was created from the reverted `main`.
- This branch restores the feature content by reverting the revert commit, so the next PR can run through the normal CI and merge flow.
- PR `#83` initially had no checks because the branch workflow contained a duplicate `coverage-macos` job definition, which GitHub rejected at dispatch time.
- The duplicate job was removed on the reapply branch so CI can attach and run normally.
