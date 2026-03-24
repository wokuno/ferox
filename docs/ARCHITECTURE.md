# Ferox Architecture

This document describes the current runtime architecture with emphasis on the
parallel simulation and performance-sensitive paths.

## Top-Level Layout

Ferox is organized into shared, server, client, and GUI modules:

- `src/shared/`: core types, protocol, networking helpers, utils
- `src/server/`: world state, simulation, atomic engine, threadpool, orchestration
- `src/client/`: terminal client
- `src/gui/`: SDL-based GUI client

The server is the simulation authority. Clients receive serialized world state
updates and send commands (pause, speed control, selection, reset, and manual
spawn requests). Manual spawn requests now also receive immediate structured
accept/reject feedback over `MSG_ACK` or `MSG_ERROR` instead of relying only on
later snapshot observation, and colony-selection requests now use the same
status surface so clients can distinguish accepted selection, clear-selection,
and missing-target failures immediately.

## Server Execution Model

The server process uses two long-lived threads plus worker threads:

- accept thread: accepts and registers client sessions
- simulation thread: runs tick loop and broadcasts world snapshots
- worker pool (`threadpool`): executes simulation phase work items

Client list operations are protected by `clients_mutex`. Simulation state updates
happen in the simulation pipeline, with heavy work delegated to the threadpool.

## Simulation Pipeline

Two execution paths exist:

- classic path via `simulation_*` and `parallel_*`
- atomic path via `atomic_tick` (current performance focus)

The atomic path (`src/server/atomic_sim.c`) executes:

1. behavior-layer refresh on `World` (nutrients, toxins, signals, alarms)
2. parallel age phase
3. parallel spread phase (CAS-based claims)
4. barrier and spread delta application
5. stat sync back into `World`
6. colony dynamics refresh every tick (stress, dormancy, biofilm, drift, learning)
7. serial maintenance phases at cadence `FEROX_ATOMIC_SERIAL_INTERVAL`

Serial maintenance includes mutate/division/recombination plus horizontal gene
transfer and avoids running at full frequency on every tick when cadence tuning
allows lower overhead.

## Atomic World Design

`AtomicWorld` wraps `World` with lock-free update structures:

- `DoubleBufferedGrid`: two atomic cell buffers (read/current + write/next)
- `AtomicColonyStats[]`: cacheline-aligned per-colony counters
- precomputed region work descriptors and reusable submit argument vectors
- optional spread frontier index list for sparse scheduling
- dedicated phase workers for lower-overhead phase execution
- cacheline-padded `AtomicSpreadSharedState` and `AtomicPhaseSharedState` blocks so frequently-updated spread/frontier and phase-coordination fields stay isolated from colder `AtomicWorld` metadata

Key structs are defined in:

- `src/server/atomic_sim.h`
- `src/shared/atomic_types.h`

### Atomic Field Invariants

The atomic-order audit for `#115` treats the current atomic simulation path as a
double-buffered ownership system, not as a general lock-free publication graph.
That scope matters because most atomic fields only need uniqueness or numeric
integrity, not cross-struct happens-before edges.

- `AtomicCell.colony_id`: the ownership word for a single cell. During spread,
  workers read only from the current buffer and compete only on the next buffer.
  A successful CAS must make the claim unique, but it does not publish other
  metadata to concurrent readers in the same phase, so relaxed ordering is the
  target for claim/read/write operations on the cell buffers.
- `AtomicCell.age`: a per-cell scalar copied or incremented within a phase after
  buffer ownership is already defined. No code relies on reading `age` as a
  publication fence for another field, so relaxed load/store/RMW is sufficient.
- `AtomicColonyStats.cell_count`: a numeric aggregate updated during sync and
  spread-delta application. Correctness depends on atomic arithmetic and the
  barrier between spread and apply, not on ordered publication to unrelated
  fields.
- `AtomicColonyStats.max_cell_count`: monotonic max-tracking only. The invariant
  is `max_cell_count >= cell_count peak observed so far`; readers tolerate stale
  values between sync points, so relaxed CAS/load/store is acceptable.
- `World.next_colony_id`: uniqueness source only. Callers do not use a fetched id
  as a publication barrier for the rest of the `Colony` payload, so relaxed
  load/fetch-add is the intended policy.
- `phase_wait_eq()` acquire loads remain the exception: when the helper is used
  as a wait primitive, wake-up code may immediately inspect state guarded by the
  waited-on value, so the polling side keeps acquire semantics.

Non-atomic coordination fields in `ThreadPool`, `AtomicSpreadSharedState`, and
`AtomicPhaseSharedState` remain mutex/condvar-protected and are deliberately out
of scope for this audit.

## Threadpool Design

The threadpool evolved from a simple global FIFO to a mixed scheduler:

- per-worker queues (`WorkerQueue`) for locality and reduced global contention
- lock-free fast queues (`FastTaskQueue`) for hot paths
- owner-local submit and batching controls
- worker-generated follow-on submit fast path that reuses thread-local worker identity to push directly into the submitting worker's local queue before falling back to shared ingress paths
- work stealing controls (`steal_probe_limit`, `steal_batch_size`)
- profile presets via `FEROX_THREADPOOL_PROFILE`
- optional telemetry in `ThreadPoolTelemetry`
- cacheline-padded `ThreadPoolHotCounters` so queue-depth / active-task updates do not share a line with colder pool pointers and synchronization objects

The fast path is intentionally narrow-scope: it only targets single-task submits
issued by an active pool worker while that worker is already executing Ferox
threadpool work. Those follow-on tasks stay thread-local until the current task
finishes, which avoids an extra shared-queue enqueue/dequeue round-trip for the
worker-chained case. External producers, non-worker threads, and batch submit
calls continue to use the existing shared submit path so semantics, wakeups, and
fallback behavior stay unchanged.

Relevant files:

- `src/server/threadpool.h`
- `src/server/threadpool.c`

## Data Model Highlights

Core runtime structures are in `src/shared/types.h`:

- `World`: grid cells, colony array, id-index map, environmental layers
- `Colony`: genome + state + lineage + visual/color data
- `Genome`: spread, social, environment, interaction, survival, and strategy traits

Performance-sensitive additions include:

- `world->colony_index_map` for faster id-to-colony lookups
- cacheline-aware atomic stats and queue metadata alignment

## Hardware Detection and Runtime Tuning

`src/server/hardware_profile.c` probes the local host before server startup and
selects one of three runtime targets:

- `cpu`
- `apple`
- `amd`

The target selection currently tunes the existing CPU execution path rather than
switching to a separate GPU kernel backend. The selection layer is responsible
for:

- detecting OS, architecture, CPU count, CPU vendor, and GPU vendor
- recognizing Apple Silicon hosts and AMD GPU hosts
- applying default scheduler profile and atomic cadence values unless the user
  already provided explicit env overrides
- reporting the chosen target via `ferox_server --print-hardware`

Current defaults:

- `cpu`: `latency` on small hosts, otherwise `balanced`; serial interval `5`
- `apple`: `latency`; serial interval `4`; frontier dense threshold `18%`
- `amd`: `throughput`; serial interval `6`; frontier dense threshold `12%`

This keeps the runtime selection logic separate from the simulation kernel so a
future Metal or AMD GPU backend can plug into the same target-selection layer.

## Protocol and State Transport

Protocol encode/decode lives in `src/shared/protocol.c`.

World snapshots include colony metadata and grid data, with adaptive
serialization behavior in the grid codec path to avoid RLE regressions on noisy
maps. On the server side, snapshot preparation now builds the outgoing grid and
colony centroid metadata in a single pass over the world grid instead of doing
one full-grid rescan per active colony. Worlds larger than the inline snapshot
threshold ship colony metadata in `MSG_WORLD_STATE` and stream the grid through
ordered `MSG_WORLD_DELTA` chunks. The GUI renderer now resolves colony ids from
grid cells with binary search over the sorted colony metadata instead of a full
linear scan per visible cell. Protocol performance is tracked by
`test_perf_unit_protocol` and `test_performance_profile`.

The current protocol generation is documented as `PROTOCOL_VERSION == 1`, but
that version is not yet serialized in the transport header or handshake. Until
explicit negotiation exists, client/server compatibility is defined by shipping
matching builds and keeping `docs/PROTOCOL.md` aligned with the live wire format.

## Performance Instrumentation Architecture

Performance validation is built into the repo (not a one-off script):

- unit: `test_perf_unit_world`, `test_perf_unit_protocol`
- component: `test_perf_components`, `test_threadpool_profile_scan`
- system: `test_threadpool_microbench`, `test_performance_profile`
- multi-run aggregation: `scripts/perf_scenarios.py`

`test_performance_profile` also carries an additive architecture-specific atomic
microbench lane that emits common relaxed-operation costs on every host and
extra ordered-operation probes on x86/x86_64 versus arm/aarch64. This keeps the
CI path stable while still surfacing platform-specific atomic cost differences.

Reference docs:

- `docs/PERF_RUNBOOK.md`
- `docs/PERF_TARGETS.md`
- `docs/PERFORMANCE_BACKLOG.md`
