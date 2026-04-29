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
updates and send commands (pause, speed control, selection, reset).

## Server Execution Model

The server process uses two long-lived threads plus worker threads:

- accept thread: accepts and registers client sessions
- simulation thread: runs tick loop and broadcasts world snapshots
- worker pool (`threadpool`): executes simulation phase work items

Client list operations are protected by `clients_mutex`. Simulation state updates
happen in the simulation pipeline, with heavy work delegated to the threadpool.

## Network Transport Model

Accepted sockets and client sockets run in nonblocking mode after connection
setup. The protocol layer owns frame assembly with `ProtocolRecvState`, so a
partial TCP header or payload is retained across simulation/render ticks instead
of forcing the socket back to blocking mode.

World broadcasts are serialized once per tick and queued to each active client as
an ordered send batch. `clients_mutex` protects the linked client list and batch
queue mutation only; actual socket writes are pumped after the lock is released.
Each client has one active batch and one pending batch. New world updates may
replace only unsent queued work, never a batch that has already written bytes to
the socket. When an unsent world batch is replaced, any older pending batch is
also discarded so stale detail or chunk data cannot trail a newer snapshot.
World batches also carry a baseline candidate keyed by the parent
`MSG_WORLD_STATE` header sequence. The candidate is inserted into the client's
bounded baseline ring only after the batch writes its first byte, so coalesced
unsent work never becomes a future delta baseline. Client ACKs mark completed
baseline entries after inline-grid snapshots or final accepted grid chunks. The
ring stores full-grid candidates for future delta generation only; current wire
payloads are still full snapshots or large-world grid chunks. The ring holds at
most eight `uint16_t` grids per connected client, so retained ring memory is
bounded to `16 * width * height` bytes per client before allocator overhead. An
active or pending unsent batch can temporarily hold one additional candidate
grid until it is first written, coalesced, or freed. Per-client transport stats
track queued/replaced world batches, send backpressure, frames/bytes sent,
completed batches, ACKed baselines, and tick lag from the latest queued update
to the latest started or ACKed update.

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

Key structs are defined in:

- `src/server/atomic_sim.h`
- `src/shared/atomic_types.h`

## Threadpool Design

The threadpool evolved from a simple global FIFO to a mixed scheduler:

- per-worker queues (`WorkerQueue`) for locality and reduced global contention
- lock-free fast queues (`FastTaskQueue`) for hot paths
- owner-local submit and batching controls
- work stealing controls (`steal_probe_limit`, `steal_batch_size`)
- profile presets via `FEROX_THREADPOOL_PROFILE`
- optional telemetry in `ThreadPoolTelemetry`

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
ordered `MSG_WORLD_DELTA` chunks. Large-world chunk serialization reads colony
ids directly from the `World.cells[].colony_id` strided field, avoiding the
previous per-chunk temporary `uint16_t` copy. The GUI renderer now resolves
colony ids from grid cells with binary search over the sorted colony metadata
instead of a full linear scan per visible cell. Protocol performance is tracked
by `test_perf_unit_protocol` and `test_performance_profile`.

## Performance Instrumentation Architecture

Performance validation is built into the repo (not a one-off script):

- unit: `test_perf_unit_protocol`
- component: `test_simd_eval`, `test_perf_components`
- system: `test_performance_eval`, `test_performance_profile`
- multi-run aggregation: `scripts/perf_scenarios.py`

Reference docs:

- `docs/PERF_RUNBOOK.md`
- `docs/PERFORMANCE_BACKLOG.md`
