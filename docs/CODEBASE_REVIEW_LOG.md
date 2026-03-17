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
- PR `#83` then failed the macOS build because `prepare_region_tasks()` still referenced `submit_args` after that array had been moved into `submit_region_tasks()`.
- The stale `submit_args[task_idx] = work;` line was removed so the branch now compiles again.

## Warning Cleanup

Status: In progress

Progress:
- Current `./scripts/build.sh` emits test-only warnings from `tests/test_client_input_surface.c` and `tests/test_renderer_logic_surface.c`.
- The warning cleanup branch removes unused local variables in the stdin helper test and drops an unused `count_substring()` helper from the renderer surface test.
- The warning cleanup branch removes unused local variables in the stdin helper test.
- A follow-up build still warned on the zero-length path for `bytes`; the helper now asserts `bytes != NULL || len == 0` so the parameter is semantically used and the contract is explicit.
- Because Release builds compile out `assert`, the helper now also uses `(void)bytes;` so the parameter remains intentionally referenced in all build modes.
- CI for PR `#84` showed that `count_substring()` is still used by the border/highlight renderer test, so that helper was restored.
- Release builds also compile out the renderer assertions, so the border/colony counts are now computed into locals and explicitly referenced before assertion checks.

## Preview Workflow Fix

Status: In progress

Progress:
- The `Update Preview GIF` workflow failed in `Commit updated preview` after checking out a detached `head_sha`.
- The push refspec now uses `HEAD:refs/heads/<branch>` so Git can push back to branch names containing slashes from a detached checkout.

## Main-only Preview Refresh

Status: In progress

Progress:
- Preview GIF updates are now restricted to `main`.
- The workflow only runs after successful `CI` runs triggered by `push` events on `main`, or manual dispatches launched from `main`.
- Manual dispatch remains allowed on any branch.
- The target-branch resolution step now rejects non-`main` branches only for automatic `workflow_run` executions.

## Flamechart Profiling

Status: In progress

Plan:
- Use `samply record --save-only` on `build/tests/test_performance_eval` so the run is reproducible and the profile data can be reopened later.
- Capture at least a default run and a heavier `FEROX_PERF_SCALE=5` run.
- Summarize the hottest symbols and likely bottlenecks in the review log before proposing follow-up changes.

Progress:
- `xctrace` is present on the macOS host but unusable because the active developer directory points at Command Line Tools instead of full Xcode.
- `samply` was installed locally and `samply setup -y` succeeded, but repeated `samply record` attempts still failed with `Unknown(1100)`.
- Linux profiling on `paco` is the better fallback, but `/home/wokuno/ferox` is a dirty worktree on `main` and is `103` commits behind `origin/main`.
- `paco` is running openSUSE Leap 16.0 with `cmake`, `ninja`, `gcc`, and `git` already installed.
- `perf` is available from the openSUSE package repositories, so the next safe step is to install profiling tools there and work from a fresh clone instead of modifying the stale local checkout.
- `perf` installation on `paco` is blocked by a Perl dependency downgrade, so the lower-risk path is Linux `samply` instead.
- `rust`, `cargo`, and `samply` are now installed on `paco`; profiling also required lowering `/proc/sys/kernel/perf_event_paranoid` from `2` to `1`.
- A fresh profiling clone was created at `/home/wokuno/ferox-profile` from current `origin/main` so the stale `/home/wokuno/ferox` checkout remains untouched.
- `samply` captures completed successfully for `build/tests/test_performance_eval` and `FEROX_PERF_SCALE=5`.
- The plain `Release` build produced mostly raw addresses, so an additional `RelWithDebInfo` build with `-O2 -g -fno-omit-frame-pointer` was created at `/home/wokuno/ferox-profile/build-relwithdebinfo`.
- The symbolized `FEROX_PERF_SCALE=5` Linux profile points at three dominant areas:
  - serial simulation diffusion work in `diffuse_scalar_field()` and nearby `utils_clamp_f()` / `calculate_biomass_pressure()` paths
  - atomic simulation social-neighborhood work in `calculate_social_context()` / `calculate_social_influence()`
  - scheduler overhead around `submit_region_tasks()`, `worker_thread()`, and atomic tick orchestration
- The benchmark counters from the same profiled run are consistent with the sample data:
  - `simulation_tick (serial)` took about `2355 ms`
  - `atomic phase: serial core` took about `1792 ms` of `2483 ms` total
  - `atomic phase: spread` took about `585 ms`
  - `threadpool tiny tasks` remained about `1.72x` slower than chunked submit and `28.43x` slower than batched tasks
- First optimization pass:
  - added a dedicated `scratch_eps` buffer to `World` so diffusion can precompute per-cell EPS once per field update instead of repeating colony lookups in each neighbor edge calculation
  - replaced inner-loop `powf()` transport attenuation with exact fast paths for the exponents currently used in practice (`1.0`, `1.5`, `2.0`, `3.0`, `4.0`)
  - removed per-tick rebuilding of atomic region batch argument arrays by caching `region_submit_args` inside `AtomicWorld`
- Next step is targeted verification and a benchmark rerun to see how much of the diffusion hotspot moved before changing the atomic social scan.
- Second optimization pass:
  - `atomic_spread_region()` now preloads its 8-neighbor occupancy state once, skips chemotaxis/social-context work entirely for cells with no empty neighbor, and only computes spread probability for actually-empty target cells
  - this directly targets the profiled `calculate_social_context()` / `calculate_social_influence()` hotspot without changing the spread rules themselves
- Third optimization pass in progress:
  - diffusion still pays per-edge attenuation math even when no cells have non-zero EPS/biofilm influence
  - next change is an exact zero-EPS fast path in `diffuse_scalar_field()` so the common no-biofilm case uses a plain 4-neighbor stencil instead of the more expensive attenuated path
- Verification after the first two optimization passes:
  - targeted regressions still pass: `SimulationLogicTests`, `SimdEvalTests`
  - benchmark deltas on the local macOS host:
    - before the atomic spread-path change:
      - `simulation_tick (serial)` about `112.46 ms`
      - `atomic_tick (4 threads)` about `117.16 ms`
      - atomic/serial ratio about `1.04x`
      - atomic spread phase about `23.27 ms`
      - atomic total about `119.83 ms`
    - after the atomic spread-path change:
      - `simulation_tick (serial)` about `110.01 ms`
      - `atomic_tick (4 threads)` about `102.72 ms`
      - atomic/serial ratio about `0.93x`
      - atomic spread phase about `8.69 ms`
      - atomic total about `88.46 ms`
  - scheduler overhead also improved in the same run:
    - `threadpool tiny/chunked-submit ratio` moved from about `1.85x` in the earlier Linux profile and `1.18x` after the first local pass to about `1.23x` after the atomic spread-path cleanup on the current run
    - `threadpool tiny/batched ratio` is now about `5.43x` on the current local run
  - `test_performance_eval` still reports one environment-sensitive failure in `server_broadcast_path_breakdown_eval` when `server_create()` returns `NULL`; the simulation and SIMD regression suites remain green
- Result of the zero-EPS diffusion fast path:
  - targeted regressions still pass: `SimulationLogicTests`, `SimdEvalTests`
  - the change is behavior-preserving, but it did not materially move the local serial benchmark on the current scenario:
    - `simulation_tick (serial)` stayed roughly flat at about `128.30 ms`
    - `atomic_tick (4 threads)` remained improved at about `102.22 ms`
    - atomic/serial ratio remained favorable at about `0.91x`
  - interpretation: the exact no-EPS branch is worthwhile cleanup for the common zero-biofilm case, but the current benchmark’s remaining serial cost is dominated by other work, not this specific attenuation branch
  - next serial targets should shift from `diffuse_scalar_field()` itself toward the repeated neighborhood work around `calculate_biomass_pressure()` and adjacent spread heuristics
- Fourth optimization pass:
  - `simulation_spread()` now computes source-cell invariants once per occupied cell instead of once per candidate direction
  - the inner loop no longer uses `world_get_cell()` for source/target access
  - target-cell enemy and curvature scans were merged into one `analyze_target_neighborhood()` pass, eliminating duplicate 8-neighbor rescans per empty target
  - directional weight lookup in the hot loop now uses `spread_weights[d]` directly instead of remapping through `get_direction_weight()`
- Verification after the serial spread rewrite:
  - targeted regressions still pass: `SimulationLogicTests`, `SimdEvalTests`
  - local benchmark deltas vs the prior pass:
    - `simulation_tick (serial)` improved from about `128.31 ms` to about `117.82 ms`
    - `frontier telemetry compute` improved from about `69.71 ms` to about `66.33 ms`
    - `atomic_tick (4 threads)` stayed roughly flat at about `103.21 ms`
    - `atomic_tick baseline serial` in the same run improved to about `103.16 ms`
  - interpretation: the repeated neighborhood rescans in `simulation_spread()` were a real serial bottleneck, and this pass produced the first meaningful serial-side gain after the earlier atomic improvements
  - the remaining visible test issue is unchanged: `server_broadcast_path_breakdown_eval` still fails when `server_create()` returns `NULL`
- Sixth optimization pass in progress:
  - `calculate_env_spread_modifier()` still rescans quorum/local-density data for the same source cell on every candidate direction
  - next change is to precompute source-cell local density once per occupied cell and feed it into the environmental modifier, removing one repeated radius scan from the hot spread loop
- Seventh optimization pass in progress:
  - serial spread still pays per-direction helper cost even when the genome makes those helpers inert
  - next change is to skip scent work when the genome has no effective scent response and skip quorum-density scans when density tolerance makes the quorum penalty a no-op
- Eighth optimization pass in progress:
  - `calculate_env_spread_modifier()` still runs for every empty target even when the genome has effectively no nutrient, toxin, edge, or quorum response
  - next change is to add a per-source-cell fast path so neutral genomes use `env_modifier = 1.0f` without paying the helper cost
- Atomic-path follow-up:
  - every successful atomic claim still performs a lock-free max-population update in addition to the population increment
  - next change is to remove that per-claim atomic max CAS loop and refresh historical max population during the serial sync path instead
- Atomic structural plan:
  - replace queued per-phase threadpool submission with persistent atomic-phase workers owned by `AtomicWorld`
  - precompute fixed region assignments once per atomic world instead of rebuilding them each phase
  - fuse `age + spread` inside `atomic_tick()` to remove one submit/wait cycle per tick while preserving separate phase entry points for diagnostics and component tests
- Cross-machine comparison after applying the current patch set to `paco`:
  - `paco` targeted regressions also pass: `SimulationLogicTests`, `SimdEvalTests`
  - `paco` `test_performance_eval` now passes all cases, including `server_broadcast_path_breakdown_eval`
  - current `paco` benchmark snapshot:
    - `simulation_tick (serial)`: about `185.45 ms`
    - `atomic_tick (4 threads)`: about `149.77 ms`
    - atomic/serial ratio: about `0.88x`
    - atomic phase spread: about `23.48 ms`
    - atomic phase serial core: about `108.79 ms`
    - `threadpool tiny/chunked-submit ratio`: about `1.96x`
    - `threadpool tiny/batched ratio`: about `22.95x`
  - interpretation by machine:
    - local `Apple M1 Max`: recent gains came from serial spread cleanup and branch / neighborhood-scan reduction
    - `paco` Xeon: absolute gains carried over, but thread scaling is still poor (`4-thread` about `0.98x` vs `1-thread` in the thread-scaling eval), which points back to region granularity, scheduler overhead, and likely cache / false-sharing sensitivity
  - next optimization split:
    - good for both: further removal of repeated neighborhood analysis and pointer chasing
    - especially valuable for `paco`: coarser atomic region scheduling and less cross-thread contention / false sharing
- Fifth optimization pass in progress:
  - current atomic region layout uses `4x1` stripes for `4` threads
  - that layout is suspicious on `paco` because it creates long shared boundaries and poor locality for neighborhood-heavy spread work
  - next experiment is a `2x2` layout for the 4-thread case to see whether it improves Xeon scaling without regressing the local M1 run
- Outcome of the `2x2` 4-thread layout experiment:
  - local M1 run: mixed; `atomic_tick (4 threads)` improved slightly, but large-grid `sync_to_world` regressed badly
  - `paco` Xeon run: rejected
    - `simulation_tick (serial)` improved to about `170.01 ms`, but `atomic_tick (4 threads)` regressed to about `170.04 ms`
    - atomic/serial ratio worsened to about `1.05x`
    - thread scaling improved somewhat (`4-thread` about `1.11x` vs `1-thread`), but the absolute 4-thread result got worse than the prior `4x1` layout
  - conclusion: keep the original `4x1` layout for now; the root Xeon problem is not just tile shape, it is deeper scheduler / contention overhead relative to the amount of per-region work
- Follow-up backlog:
  - fix the `server_broadcast_path_breakdown_eval` / `server_create()` failure path after the current diffusion optimization pass
  - continue reducing serial time in `diffuse_scalar_field()` and nearby biomass-pressure work before shifting focus back to server-side test hygiene
- CPU context for the next optimization passes:
  - local development host: `Apple M1 Max`, `10` physical / logical CPUs
  - `paco`: `Intel Xeon E-2124 @ 3.30GHz`, `4` physical CPUs, no SMT, AVX2-capable, smaller cache hierarchy
  - interpretation:
    - branch reduction and memory-locality work should help both machines
    - explicit x86 AVX2-friendly vector work is more likely to benefit `paco`
    - reducing pointer chasing and repeated neighborhood scans is especially attractive for the M1 due to its strong core performance and memory-system sensitivity to irregular access
- Server benchmark follow-up:
  - `server_broadcast_path_breakdown_eval` does not need a real listening socket, but today it calls `server_create()` which always opens one
  - fix direction: add a headless/in-process server constructor for benchmark-only snapshot work instead of weakening normal `server_create()` semantics
- Server benchmark fix:
  - added `server_create_headless()` for in-process benchmark/snapshot work that does not accept clients
  - switched `server_broadcast_path_breakdown_eval` to use the headless constructor so local benchmark runs no longer depend on socket bind/listen success
  - normal `server_create()` semantics remain unchanged for integration and runtime paths
- Server benchmark status:
  - the broad local `test_performance_eval` run now passes `server_broadcast_path_breakdown_eval`
  - snapshot build metrics on the passing local run improved to about:
    - `broadcast build snapshot`: `3.83 ms`
    - `broadcast build+serialize`: `6.37 ms`
    - `broadcast end-to-end (0 clients)`: `6.27 ms`
- Focused perf test plan:
  - add a separate component-level perf suite instead of growing the broad benchmark further
  - first component targets:
    - `simulation_update_nutrients()` to isolate diffusion/update cost
    - `simulation_spread()` to isolate the serial spread heuristics
    - `atomic_spread()` + barrier to isolate the atomic spread phase
    - `server_build_protocol_world_snapshot()` via `server_create_headless()` to isolate snapshot construction without socket noise
- The `server_broadcast_path_breakdown_eval` constructor issue remains on the list and should be finished after the component perf suite is wired into the build so the benchmark path can be revalidated directly.
- Component benchmark methodology follow-up:
  - the first component suite version still lets worlds evolve across iterations, which makes spread/update timings drift and complicates before/after comparisons
  - next improvement is to benchmark kernels against a pool of prebuilt worlds and run one measured kernel pass per world, so the component timing reflects a stable workload instead of a changing simulation state
- Stable component benchmark snapshot after moving to prebuilt-world pools (`FEROX_PERF_SCALE=5`):
  - `simulation_update_nutrients`: about `60.50 ms` for `400` prepared worlds
  - `simulation_spread`: about `516.81 ms` for `200` prepared worlds before the later spread fast paths
  - `atomic_age` phase: about `15.21 ms` for `300` iterations
  - `atomic_spread` phase: about `22.62 ms` for `300` iterations
  - `server snapshot build`: about `36.01 ms` for `800` iterations
  - interpretation: the serial spread kernel is still the dominant direct optimization target by a wide margin
- Result after the later neutral-genome/env/perception fast paths (`FEROX_PERF_SCALE=5`):
  - `simulation_update_nutrients`: about `62.00 ms`
  - `simulation_spread`: about `39.68 ms`
  - `atomic_age` phase: about `16.78 ms`
  - `atomic_spread` phase: about `20.57 ms`
  - `server snapshot build`: about `35.19 ms`
  - interpretation: the serial spread component dropped sharply on the stabilized component benchmark, so the next step is to confirm how much of that translates to the broad end-to-end benchmark
- Broad benchmark snapshot after the later spread fast paths:
  - `server_broadcast_path_breakdown_eval`: passing locally
  - `simulation_tick (serial)`: about `101.03 ms`
  - `atomic_tick (4 threads)`: about `101.60 ms`
  - atomic/serial ratio: about `1.01x`
  - `threadpool tiny/chunked-submit ratio`: about `1.01x`
  - `threadpool tiny/batched ratio`: about `5.30x`
  - interpretation:
    - serial simulation improved substantially versus the earlier local baseline around `128 ms`
    - the benchmark-path server issue is fixed for local runs
    - the next remaining high-value target is still atomic scaling, especially on `paco`, where absolute atomic gains carried over but multi-thread scaling remains weak
- Atomic structural plan now being implemented:
  - replace queued per-phase threadpool submission with persistent atomic-phase workers owned by `AtomicWorld`
  - precompute fixed region assignments once per atomic world instead of rebuilding them every phase
  - fuse `age + spread` inside `atomic_tick()` to remove one submit/wait cycle per tick while preserving separate phase entry points for diagnostics and component tests
  - keep the local runner backend separate from the simulation kernels so it can later map more directly to an OpenSHMEM-style PE dispatch model
- Control-plane design constraint:
  - avoid introducing mutex/cond coordination into the new atomic runner
  - use atomics for phase generation, work claiming, completion tracking, and shutdown signaling, with a bounded spin/yield/sleep backoff while idle
- Atomic structural runner result:
  - implemented fixed region preparation at `AtomicWorld` creation time, a persistent atomic worker backend, and a fused non-breakdown `age + spread` path in `atomic_tick()`
  - the worker control plane uses atomics for phase generation, shutdown, and completion tracking; it does not rely on mutex/cond coordination for dispatch
  - region ownership is now fixed per participant during a phase rather than dynamically rebuilt and requeued through the generic threadpool path
- Verification after the structural atomic pass:
  - targeted regressions still pass: `SimulationLogicTests`, `SimdEvalTests`, `PerformanceComponentTests`
  - broad benchmark still passes completely: `test_performance_eval` `13/13`
  - representative local benchmark snapshot after the fused runner change:
    - `simulation_tick (serial)`: about `112.99 ms`
    - `atomic_tick (4 threads)`: about `105.47 ms`
    - atomic/serial ratio: about `0.93x`
    - `atomic_tick (1/2/4 threads)`: about `72.01 / 70.02 / 60.85 ms`
    - speedup vs `1` thread: `2-thread=1.03x`, `4-thread=1.18x`
    - atomic phase breakdown remains dominated by the serial core:
      - `age`: about `8.19 ms`
      - `spread`: about `9.92 ms`
      - `serial core`: about `76.51 ms`
      - `total`: about `105.02 ms`
  - focused component suite interpretation:
    - isolated `atomic_age()` and `atomic_spread()` phase timings did not improve under the persistent runner
    - the current win is therefore primarily from removing one phase-launch / wait cycle and bypassing generic threadpool queueing in the end-to-end tick path, not from making the spread kernel itself faster
  - design implication for future SHMEM work:
    - the new fixed-region, explicit-phase layout is a better fit for mapping regions onto PEs than the prior queue-submission model
- `paco` validation of the structural atomic pass:
  - validated on a fresh checkout at `/home/wokuno/ferox-validate-atomic` with the current local patch set applied
  - `test_performance_eval` still passes fully there: `13/13`
  - `PerformanceComponentTests` also pass there, including `FEROX_PERF_SCALE=5`
  - current `paco` broad benchmark snapshot:
    - `simulation_tick (serial)`: about `196.69 ms`
    - `atomic_tick (4 threads)`: about `174.91 ms`
    - atomic/serial ratio: about `0.95x`
    - `atomic_tick (1/2/4 threads)`: about `116.03 / 101.66 / 88.11 ms`
    - speedup vs `1` thread: `2-thread=1.14x`, `4-thread=1.32x`
  - current `paco` component snapshot at `FEROX_PERF_SCALE=5`:
    - `simulation_update_nutrients`: about `102.76 ms`
    - `simulation_spread`: about `41.86 ms`
    - `atomic_age` phase: about `3.87 ms`
    - `atomic_spread` phase: about `22.98 ms`
    - `server snapshot build`: about `52.89 ms`
  - comparison to the earlier pre-structural `paco` snapshot:
    - earlier broad throughput: `simulation_tick (serial)` about `185.45 ms`, `atomic_tick (4 threads)` about `149.77 ms`, ratio about `0.88x`
    - current broad throughput is worse in absolute time, even though the thread-scaling microbenchmark improved from roughly `0.98x` to `1.32x` at `4` threads
  - interpretation:
    - the persistent runner improved the dedicated scaling case on the Xeon, but it likely hurts the long serial section of the broad tick because idle worker threads remain active in the atomic wait loop during the serial core
    - this means the current lock-free runner is a net win on the local M1 path, but not yet a validated net win on `paco`
- Linux follow-up for the persistent atomic runner:
  - next experiment is to keep the atomics-only control plane but replace Linux idle polling with futex wait/wake on `phase_generation` and `phase_completed_workers`
  - hypothesis: this should preserve the cheaper explicit phase dispatch while stopping idle worker threads from stealing CPU during the long serial core on 4-core Xeon hosts like `paco`
- Linux futex follow-up result:
  - the first full futex version improved broad `paco` throughput but exposed bad behavior in the scaled component suite, so the completion wait path was reverted to the bounded atomic backoff loop
  - the kept version uses futex wait/wake only for idle worker sleep on `phase_generation`; completion tracking remains atomic polling with bounded backoff
  - this preserves the lock-free control plane while parking idle workers during the long serial core on Linux
  - final local macOS snapshot after narrowing futex use:
    - `simulation_tick (serial)`: about `119.22 ms`
    - `atomic_tick (4 threads)`: about `115.69 ms`
    - atomic/serial ratio: about `0.97x`
    - `atomic_tick (1/2/4 threads)`: about `78.81 / 67.20 / 65.78 ms`
    - `PerformanceComponentTests` at `FEROX_PERF_SCALE=5` complete normally again
  - final `paco` snapshot after narrowing futex use:
    - `simulation_tick (serial)`: about `205.70 ms`
    - `atomic_tick (4 threads)`: about `171.31 ms`
    - atomic/serial ratio: about `0.87x`
    - `atomic_tick (1/2/4 threads)`: about `110.90 / 77.78 / 79.15 ms`
    - `test_performance_eval`: `13/13` passed
    - `FEROX_PERF_SCALE=5 test_perf_components`: passed
  - comparison against the earlier pure-polling persistent runner on `paco`:
    - broad throughput improved from about `174.91 ms` to about `171.31 ms`
    - broad ratio improved from about `0.95x` to about `0.87x`
    - the scaled component suite stopped timing out
  - interpretation:
    - parking idle workers during the serial core helps the Xeon path
    - the remaining performance bottleneck is still the serial core itself, not atomic dispatch
- Wait-backend refactor:
  - moved platform-specific worker park/unpark logic out of `atomic_sim.c` and into a dedicated `phase_wait` backend layer
  - `atomic_sim.c` now depends only on atomic sequencing plus a narrow `phase_wait_eq` / `phase_wake_all` / `phase_wait_backoff` interface
  - this keeps the execution model atomic-first while isolating OS-specific sleep behavior behind a replaceable backend boundary

## 2026-03-06 Performance Research Pass

Status: In progress

Goals:
- rerun current tests and perf baselines
- add more focused serial-core component tests
- do outside research and map it back to concrete Ferox experiments
- document the resulting backlog in a reusable form

Progress:
- Added new focused component perf coverage in `tests/test_perf_components.c` for:
  - `simulation_update_scents()`
  - `simulation_resolve_combat()`
  - `frontier_telemetry_compute()`
- Latest local `test_perf_components` `FEROX_PERF_SCALE=5` snapshot:
  - `simulation_update_nutrients`: about `67.55 ms`
  - `simulation_spread`: about `42.12 ms`
  - `simulation_update_scents`: about `31.36 ms`
  - `simulation_resolve_combat`: about `35.36 ms`
  - `frontier_telemetry_compute`: about `110.17 ms`
  - `atomic_age` phase: about `21.82 ms`
  - `atomic_spread` phase: about `28.84 ms`
  - `server snapshot build`: about `44.20 ms`
- Interpretation from the expanded component suite:
  - frontier telemetry is now clearly a first-class serial-core hotspot
  - nutrient diffusion is still the most expensive continuous-field kernel
  - combat and scent work are meaningful, but not the top item on the current local machine
  - snapshot construction is no longer the dominant user-facing cost
- Outside research notes were written up in `docs/PERFORMANCE_RESEARCH.md`.
- Research themes that mapped cleanly onto the codebase:
  - cache-aware stencil blocking / layer-condition thinking for nutrients and scents
  - dirty-region / dirty-frontier tracking as an old game-engine style optimization that also fits the biology/simulation model
  - broadphase filtering for combat and other expensive neighbor-interaction work
  - multirate updates for expensive observability or smooth fields
  - quantized/fixed-point field experiments if memory bandwidth stays dominant

Artifacts updated:
- `docs/PERFORMANCE.md`
- `docs/PERFORMANCE_RESEARCH.md`
- `tests/test_perf_components.c`

Open follow-up ideas from this pass:
1. Prototype tiled `diffuse_scalar_field()` and benchmark it directly in the component suite.
2. Prototype dirty-tile or frontier-list telemetry to replace full-grid `frontier_telemetry_compute()` scans.
3. Add combat broadphase masks so `simulation_resolve_combat()` can skip obviously empty/safe tiles.
4. Try multirate telemetry/scent update experiments in a perf branch before any high-risk quantization work.

Chosen implementation pass:
- reduce `frontier_telemetry_compute()` from two full-grid scans to one full-grid scan plus a frontier-only postpass, while also caching root-lineage resolution through the existing `colony_by_id` table
- reduce branch and index overhead in `diffuse_scalar_field()` by specializing the hot interior stencil instead of doing four boundary checks on every cell
- Implementation update:
  - `src/server/frontier_metrics.c` now uses the `colony_by_id` lookup table, caches resolved root lineages per colony id for the duration of a telemetry call, and computes sectors from a frontier-only index list after a single full-grid scan
  - `src/server/simulation.c` now uses a specialized interior stencil path in `diffuse_scalar_field()` for normal-sized grids, keeping the old branchy logic only for tiny boundary-heavy cases
- Clean validation note:
  - overlapping local perf runs during this pass were discarded and are not treated as valid comparisons
  - final comparison numbers for this implementation pass were taken from isolated runs on `paco`
- `paco` validation for the telemetry + stencil pass:
  - `test_frontier_metrics`: passed
  - `FEROX_PERF_SCALE=5 test_perf_components`: passed
  - `test_performance_eval`: passed `13/13`
  - before/after broad snapshot on `paco`:
    - previous: `simulation_tick (serial)` about `205.70 ms`, `atomic_tick (4 threads)` about `171.31 ms`, ratio about `0.87x`
    - current: `simulation_tick (serial)` about `184.09 ms`, `atomic_tick (4 threads)` about `133.13 ms`, ratio about `0.79x`
  - current component snapshot on `paco` (`FEROX_PERF_SCALE=5`):
    - `simulation_update_nutrients`: about `85.13 ms`
    - `simulation_spread`: about `41.47 ms`
    - `simulation_update_scents`: about `60.41 ms`
    - `simulation_resolve_combat`: about `37.05 ms`
    - `frontier_telemetry_compute`: about `71.73 ms`
    - `atomic_age` phase: about `4.74 ms`
    - `atomic_spread` phase: about `21.11 ms`
    - `server snapshot build`: about `43.62 ms`
  - interpretation:
    - the telemetry rewrite and branch-reduced stencil path both produced real wins on the Xeon validation host
    - the serial core is still the main broad bottleneck, but the current ranking has shifted toward scents and combat after telemetry improved
  - methodology correction:
    - a local `SimulationLogicTests` run later appeared to hang because I launched other CPU-heavy jobs at the same time
    - rerunning `./build/tests/test_simulation_logic` in isolation showed `atomic_tick_concurrent_stability` passing, so that earlier stall was treated as measurement/test scheduling interference rather than a reproduced deadlock
    - going forward, long stress tests and perf binaries should be run one at a time on the same host
  - combat broadphase follow-up:
    - `simulation_resolve_combat()` now builds a contested-border frontier during the toxin-emission pass and only runs the expensive combat resolution loop over those contested cells
    - a new focused perf case, `simulation_resolve_combat_sparse_border_component_eval`, was added to `tests/test_perf_components.c` to isolate the “many borders, no enemies” case that broadphase work should accelerate
    - local validation:
      - `test_combat_system`: passed `14/14`
      - `test_perf_components`: passed `8/8`
      - representative `FEROX_PERF_SCALE=5` snapshot on the M1 Max:
        - `simulation_update_nutrients`: about `43.03 ms`
        - `simulation_spread`: about `37.81 ms`
        - `simulation_update_scents`: about `30.16 ms`
        - `simulation_resolve_combat`: about `63.63 ms`
        - `simulation_resolve_combat_sparse_border`: about `9.08 ms`
        - `frontier_telemetry_compute`: about `62.76 ms`
    - `paco` validation:
      - `test_combat_system`: passed `14/14`
      - `FEROX_PERF_SCALE=5 test_perf_components`: passed `8/8`
      - `test_performance_eval`: passed `13/13`
      - representative `FEROX_PERF_SCALE=5` snapshot on the Xeon:
        - `simulation_update_nutrients`: about `86.77 ms`
        - `simulation_spread`: about `42.55 ms`
        - `simulation_update_scents`: about `56.37 ms`
        - `simulation_resolve_combat`: about `33.44 ms`
        - `simulation_resolve_combat_sparse_border`: about `16.84 ms`
        - `frontier_telemetry_compute`: about `73.08 ms`
      - broad `paco` snapshot for the kept state:
        - `simulation_tick (serial)`: about `185.66 ms`
        - `atomic_tick (4 threads)`: about `133.16 ms`
        - atomic/serial ratio: about `0.78x`
        - `atomic phase serial core`: about `107.52 ms`
  - rejected experiments from this pass:
    - caching EPS for `simulation_update_scents()` increased the isolated scent component cost on both machines and was reverted
    - a world-owned per-colony scalar cache for nutrient consumption and scent emission produced mixed-at-best results and a noticeably worse broad atomic result on `paco`; it was reverted
  - telemetry histogram follow-up:
    - `frontier_telemetry_compute()` now uses direct-index lineage histograms keyed by resolved root lineage id instead of a linear search through lineage buckets for every occupied and frontier cell
    - `test_frontier_metrics`: passed `3/3`
    - local M1 Max:
      - `FEROX_PERF_SCALE=5 test_perf_components`: `frontier_telemetry_compute` improved from about `62.76 ms` to about `27.81 ms`
      - broad `test_performance_eval` seeded telemetry metric improved only slightly, from about `90.03 ms` to about `88.28 ms`
    - `paco` Xeon:
      - `test_frontier_metrics`: passed `3/3`
      - `FEROX_PERF_SCALE=5 test_perf_components`: `frontier_telemetry_compute` improved from about `73.08 ms` to about `41.18 ms`
      - broad `test_performance_eval` seeded telemetry metric improved only slightly, from about `112.71 ms` to about `111.54 ms`
    - interpretation:
      - the direct histogram rewrite clearly removes real overhead in the focused telemetry kernel
      - the seeded telemetry benchmark in `test_performance_eval` is dominated by additional world-state shape/size factors, so the end-to-end gain there is much smaller than the isolated component gain
  - snapshot + no-biofilm transport follow-up:
    - `server_build_protocol_world_snapshot()` now builds a direct `colony_id -> proto_idx` table once per snapshot instead of searching the protocol colony list during the grid pass
    - `simulation_update_nutrients()`, toxin transport, and `simulation_update_scents()` now skip EPS attenuation work entirely when no active colony has non-zero biofilm strength
    - local validation:
      - `test_perf_components`: passed `8/8`
      - `test_performance_eval`: passed `13/13`
      - representative `FEROX_PERF_SCALE=5` snapshot on the M1 Max:
        - `simulation_update_nutrients`: `27.20 ms`
        - `simulation_update_scents`: `30.84 ms`
        - `simulation_resolve_combat`: `60.66 ms`
        - `simulation_resolve_combat_sparse_border`: `5.19 ms`
        - `frontier_telemetry_compute`: `24.92 ms`
        - `server snapshot build`: `31.28 ms`
      - broad local snapshot:
        - `simulation_tick (serial)`: `140.18 ms`
        - `frontier telemetry compute`: `84.05 ms`
        - `broadcast build snapshot`: `3.20 ms`
        - `atomic_tick (4 threads)`: `139.90 ms`
        - atomic/serial ratio: `0.97x`
    - `paco` validation:
      - `FEROX_PERF_SCALE=5 test_perf_components`: passed `8/8`
      - `test_performance_eval`: passed `13/13`
      - representative `FEROX_PERF_SCALE=5` snapshot on the Xeon:
        - `simulation_update_nutrients`: `69.94 ms`
        - `simulation_update_scents`: `56.59 ms`
        - `simulation_resolve_combat`: `34.45 ms`
        - `simulation_resolve_combat_sparse_border`: `11.36 ms`
        - `frontier_telemetry_compute`: `41.10 ms`
        - `server snapshot build`: `40.77 ms`
      - broad `paco` snapshot:
        - `simulation_tick (serial)`: `187.84 ms`
        - `frontier telemetry compute`: `111.02 ms`
        - `broadcast build snapshot`: `4.25 ms`
        - `atomic_tick (4 threads)`: `125.62 ms`
        - atomic/serial ratio: `0.73x`
    - interpretation:
      - the snapshot lookup rewrite is a clean keep and lowers both focused snapshot build cost and the broad broadcast-build stage
      - the no-biofilm transport fast path is a clear component-level nutrient win on both machines, while the broad serial-tick effect is smaller and mixed on the Xeon
