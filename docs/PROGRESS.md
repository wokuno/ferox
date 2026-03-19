# Ferox Progress Tracker

## Current Status

Ferox core functionality is implemented and stable. The active project phase is
performance hardening, larger-world rollout, richer colony behavior, and
hardware-aware runtime tuning.

## Completed Milestones

- Core simulation model, genetics, and world management
- Server/client architecture with network protocol and world broadcast
- Parallel simulation infrastructure (`parallel_*` and atomic simulation engine)
- Threadpool scheduler improvements (fast queue path, batching, profile presets,
  telemetry isolation, cacheline alignment)
- Hardware profile detection and accelerator-aware runtime defaults for CPU,
  Apple, and AMD targets
- Larger default world profile (`400x200`, `50` colonies) with auto-threaded
  launcher behavior
- Active colony behavior upgrades on the atomic path: signaling, alarms,
  lifecycle dynamics, motility drift, biofilm, and contact-driven gene transfer
- First explicit per-colony behavior graph pass with weighted drives/actions,
  mode selection, and focus-direction tracking
- Selected-colony explainability upgrades: top sensors, top drives, and active
  link summaries in both CLI and GUI panels
- Selected-colony panels now show fuller action ranking plus second-ranked
  graph-link summaries for better decision-chain visibility
- Active atomic runtime now includes graph-shaped serial combat maintenance, so
  smarter conflict behavior is no longer limited to the classic tick path
- Protocol codec updates including adaptive behavior for grid serialization and
  chunked large-world grid transport
- Client-side large-world rendering improvements for the GUI grid path
- Comprehensive CMake/CTest matrix with correctness, stress, and perf tests
- Performance tooling and operational docs:
  - `scripts/perf_multi_iter.py`
  - `scripts/profile.sh`
  - `scripts/profile_c2c.sh`
  - `scripts/benchmark_export.sh`
  - `docs/PERF_RUNBOOK.md`
  - `docs/PERF_TARGETS.md`
  - `docs/PERFORMANCE_BACKLOG.md`
- Recent merged follow-through:
  - `#151` architecture-specific atomic cost lane
  - `#152` hot-struct cacheline alignment/padding guardrails
  - `#153` protocol spec/conformance sync with the live wire format

## Active Workstreams

1. Rebaseline performance on the new `400x200` / `50` colony default workload
2. Rebaseline snapshot build, transport, and render costs for larger worlds
3. Continue atomic ecology/runtime parity work for larger-world behavior depth
4. Documentation synchronization with current architecture and test workflows
5. Accelerator-target groundwork for future Metal / AMD GPU offload

## Measurement Baseline (Latest Multi-Iteration Run)

Run command:

```bash
./scripts/perf_multi_iter.py -n 7 --profile balanced
```

Observed medians:

- `tiny_speedup`: `1.03x`
- `tick_speedup`: `1.15x`
- `spread_speedup`: `1.91x`
- `component atomic`:
  - `baseline` (serial interval 1): `~88.73 ms / 18 ticks`
  - `serial2`: `~47.76 ms / 18 ticks`
  - `serial3`: `~34.83 ms / 18 ticks`
  - `serial3_no_frontier`: `~71.91 ms / 18 ticks`

Current thresholds and next target values are maintained in
`docs/PERF_TARGETS.md`.

## Known Constraints

- Tiny-task synthetic metrics are noisy; single runs are not accepted as proof.
- Frontier scheduling gains are workload-density dependent.
- Performance claims are only accepted after repeated-run median confirmation.
- Apple and AMD targets currently tune the CPU atomic backend; full GPU kernel
  offload is not implemented yet.

## Next Near-Term Items

- Close tracked rollout issues `#89`, `#92`, and `#87`
- Rebaseline perf targets and history for the larger default workload
- Remove transport and broadcast scaling blockers tracked in `#91` and `#88`
- Advance the next open P0/P1 backlog issues after `#151`, `#152`, and `#153`
