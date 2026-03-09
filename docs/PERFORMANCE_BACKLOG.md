# Ferox Performance and Operations Backlog

This backlog aggregates optimization and reliability work items discovered from
code profiling, stress tests, and external systems research.

Status values:
- `open`: not started
- `in_progress`: currently being implemented
- `done`: merged and verified

Priority values:
- `P0`: highest impact / near-term
- `P1`: medium-high impact
- `P2`: important but not urgent

## Threading and Scheduling

- [x] `PERF-001` `P0` `done` Replace global queue path with owner-LIFO + thief-FIFO deques end-to-end.
- [x] `PERF-002` `P0` `done` Add adaptive spinning/searcher limits to reduce thundering herd wakeups.
- [x] `PERF-003` `P0` `done` Add queue telemetry (steal attempts/success, idle wakeups, queue depth percentiles).
- [x] `PERF-004` `P0` `done` Add bounded MPMC fast path tuning knobs (capacity, spin thresholds, fallback mode).
- [x] `PERF-005` `P1` `done` Implement batch stealing and victim randomization for better contention behavior.
- [ ] `PERF-006` `P1` `open` Add strict fairness mode for starvation-sensitive workloads.
- [ ] `PERF-007` `P1` `open` Add per-arena queues for latency-critical vs bulk background work.
- [ ] `PERF-008` `P1` `open` Add NUMA-aware two-level stealing policy on Linux servers.
- [ ] `PERF-009` `P2` `open` Add lock-free fallback queue backend feature flag for A/B evaluation.
- [x] `PERF-010` `P2` `done` Add runtime scheduler profile presets (`latency`, `throughput`, `balanced`).

## Atomics, Memory Layout, and Data Access

- [ ] `PERF-011` `P0` `open` Audit all atomic memory orders and document invariants per field.
- [ ] `PERF-012` `P0` `open` Add alignment/padding checks for all hot shared structs (queue metadata, counters).
- [ ] `PERF-013` `P0` `open` Replace global counters with sharded per-worker counters where possible.
- [ ] `PERF-014` `P1` `open` Add cacheline-aware wrappers and static assertions in shared headers.
- [ ] `PERF-015` `P1` `open` Add ARM/x86 specific microbench lane for atomic operation costs.
- [x] `PERF-016` `P1` `done` Add false-sharing diagnostics workflow (perf c2c + struct offset mapping).
- [ ] `PERF-017` `P2` `open` Introduce thread-local submit fast path for worker-generated follow-on tasks.
- [ ] `PERF-018` `P2` `open` Add optional RCU/QSBR for read-mostly metadata snapshots.

## Simulation Core

- [ ] `PERF-019` `P0` `open` Add sparse chunk/tile simulation mode for low occupancy maps.
- [ ] `PERF-020` `P0` `open` Add dirty-tile tracking to avoid full-grid sync/scan every tick.
- [ ] `PERF-021` `P0` `open` Add active frontier mode for spread/division candidate processing.
- [ ] `PERF-022` `P1` `open` Add autotuned chunk sizes and scheduler chunk policy.
- [ ] `PERF-023` `P1` `open` Add SIMD-ready SoA backend experiment for hot cell/colony fields.
- [ ] `PERF-024` `P1` `open` Optimize division/recombination using tile-local CCL + border merge.
- [ ] `PERF-025` `P1` `open` Add deterministic counter-based RNG mode (Philox-like) for parallel reproducibility.
- [ ] `PERF-026` `P2` `open` Evaluate Morton/Hilbert ordering for chunk traversal and scheduling locality.
- [ ] `PERF-027` `P2` `open` Add optional GPU-ready compute abstraction for future backend parity.

## Protocol and Networking

- [ ] `PERF-028` `P0` `open` Add world-state delta messages to reduce full snapshot transport cost.
- [ ] `PERF-029` `P0` `open` Add per-client baseline ring and robust ack-bitfield tracking.
- [ ] `PERF-030` `P1` `open` Add adaptive codec selection (RLE-only vs delta+bitpack vs LZ4/Zstd path).
- [ ] `PERF-031` `P1` `open` Add interest management prioritization to reduce irrelevant updates.
- [ ] `PERF-032` `P1` `open` Add paced UDP send policy and packet batching metrics.
- [ ] `PERF-033` `P1` `open` Add jitter-buffer and interpolation delay tuning metrics on client.
- [ ] `PERF-034` `P2` `open` Add Merkle/chunk-hash anti-entropy path for resync/recovery scenarios.

## Allocation and Memory Management

- [ ] `PERF-035` `P0` `open` Add per-thread pools for hot task descriptors and cross-thread free handoff.
- [ ] `PERF-036` `P1` `open` Add frame/phase arena for transient simulation allocations.
- [ ] `PERF-037` `P1` `open` Add allocator benchmark matrix (system malloc vs tuned allocator backends).
- [ ] `PERF-038` `P2` `open` Add generation-safe handle pool for recycled transient objects.

## Testing, Benchmarking, and Operations

- [x] `PERF-039` `P0` `done` Add dedicated threadpool microbench thresholds and regression modes.
- [x] `PERF-040` `P0` `done` Add spread-only and phase-only benchmark scenarios with metric history.
- [x] `PERF-041` `P0` `done` Add benchmark protocol docs with variance controls and runbook.
- [x] `PERF-042` `P1` `done` Add profile scripts for perf/flamegraph and Apple Instruments capture.
- [x] `PERF-043` `P1` `done` Add reproducible perf baseline files for x86_64 and Apple arm64.
- [x] `PERF-044` `P1` `done` Add nightly benchmark artifact export (CSV/JSON) with diff summary.
- [ ] `PERF-045` `P2` `open` Add perf-per-watt tracking mode for mobile/ARM hardware.

## Current Parallel Workstreams

- [x] `PERF-046` `P0` `done` Improve threadpool tiny-task scaling and validate with microbench.
- [x] `PERF-047` `P0` `done` Improve spread-phase throughput scaling and validate with profiler.
- [x] `PERF-048` `P1` `done` Expand benchmark coverage matrix and enforce stable metrics output.
- [ ] `PERF-049` `P0` `open` Implement ticketed wakeups to reduce tiny-task wake inefficiency and jitter.
- [ ] `PERF-050` `P0` `open` Add frontier hysteresis (low/high density thresholds) to avoid spread mode thrash.
- [ ] `PERF-051` `P1` `open` Prototype MPSC ingress ring + combiner drain for submit-side lock reduction.
- [ ] `PERF-052` `P1` `open` Add sparse/dense frontier representation switching (index-list vs bitset).
- [x] `PERF-053` `P1` `done` Add hardware profile detection plus CPU/Apple/AMD runtime target selection and reporting.
- [ ] `PERF-054` `P0` `open` Rebaseline performance and transport costs for the new `400x200` / `50` colony default profile (`#89`, `#91`, `#88`).
- [ ] `PERF-055` `P1` `open` Continue atomic ecology parity and richer behavior rollout (`#92`, `#87`).
