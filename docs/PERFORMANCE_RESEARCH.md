# Performance Research Notes

Date: 2026-03-06

This document captures external research, how it maps onto Ferox, and the concrete
performance experiments that look worth trying next.

## Current Measured Shape

Recent local component measurements (`FEROX_PERF_SCALE=5`) show:

- `simulation_update_nutrients`: about `67.55 ms`
- `simulation_spread`: about `42.12 ms`
- `simulation_update_scents`: about `31.36 ms`
- `simulation_resolve_combat`: about `35.36 ms`
- `frontier_telemetry_compute`: about `110.17 ms`
- `atomic_age` phase: about `21.82 ms`
- `atomic_spread` phase: about `28.84 ms`
- `server snapshot build`: about `44.20 ms`

Interpretation:

- The serial core is still the main cost center.
- Frontier telemetry is expensive enough to deserve first-class optimization attention.
- Nutrient diffusion remains the most expensive continuous-field update.
- Scent and combat are meaningful but second-tier compared with diffusion and telemetry.
- Snapshot building is no longer the top problem, but it is still worth watching because it is easy to measure and user-visible.

## Research Sources

Sources consulted or queued during this pass:

- CERN TCSC24 CPU slides:
  - <https://indico.cern.ch/event/1377435/contributions/5788940/attachments/2874689/5033970/tcsc24-cpu.pdf>
- OpenSHMEM 1.5 specification:
  - <https://openshmem.org/site/sites/default/site_files/OpenSHMEM-1.5.pdf>
- RRZE "layer condition" stencil locality notes:
  - <https://rrze-hpc.github.io/layer-condition/>
- Apple SpriteKit collision/contact guidance:
  - <https://developer.apple.com/documentation/spritekit/about-collisions-and-contacts>
- Provided device.report PDF:
  - <https://device.report/m/5ce68a282c2ccd7c44155e8ddff214eba0156258395a52d8d42c8d43858e146f.pdf>

Notes:

- The CERN and RRZE material reinforce the same basic CPU truth: once the working set falls out of cache, arithmetic is usually not the limiter. Memory traffic, stencil reuse distance, and branch predictability dominate.
- The OpenSHMEM spec is not a short-term performance source by itself, but it supports keeping the execution backend separate from simulation kernels and keeping state flat and explicit.
- The Apple collision material is a reminder that filtering and partitioning before expensive interactions is often more important than micro-optimizing the interaction itself.
- The provided device.report PDF did not yield a clear Ferox-relevant optimization direction in this pass.

## Research-Backed Performance Directions

### 1. Layer-conditioned stencil blocking

Why:

- `simulation_update_nutrients()` and `simulation_update_scents()` are stencil-style field updates.
- RRZE's layer-condition discussion maps directly onto Ferox's row-major scalar fields.
- Current code still revisits large arrays in a way that likely exceeds the cache-friendly reuse window on both the M1 and the Xeon.

Ferox mapping:

- Tile nutrient/scent/toxin updates into horizontal or 2D blocks sized to keep the active rows of `field`, `scratch`, and `scratch_eps` inside cache.
- Keep the stencil working set and destination rows hot instead of sweeping the entire grid in one giant pass.

Practical experiment:

1. Add a tiled variant of `diffuse_scalar_field()`.
2. Benchmark `simulation_update_nutrients()` and `simulation_update_scents()` side by side in `test_perf_components`.
3. Prefer simple strip-mined blocking before more exotic temporal blocking.

Why it is attractive:

- Low algorithmic risk.
- Good fit for both CPUs.
- Compatible with later SIMD work.

### 2. Dirty-region / dirty-frontier updates

Why:

- Old game-engine "dirty rectangle/grid" techniques exist to avoid rescanning unchanged space.
- Ferox already has the conceptual ingredients: occupied cells, frontier cells, changed cells, and colony-local structure.

Ferox mapping:

- Track per-tick dirty bounding boxes or dirty tiles for:
  - frontier telemetry
  - protocol snapshot generation
  - scent diffusion refresh windows
  - combat scans
- If a tile has no occupancy or no changed borders, skip telemetry/combat work there entirely.

Practical experiment:

1. Add tile-level dirty counters to `World`.
2. Measure `frontier_telemetry_compute()` on full-grid scan versus dirty-tile scan.
3. Reuse the same dirty set for snapshot encoding so the transport path and telemetry path share locality metadata.

Why it is attractive:

- This is the most "old video game trick" that cleanly applies here.
- It attacks a newly measured hotspot (`frontier_telemetry_compute`) without changing simulation semantics.

### 3. Multirate simulation

Why:

- Not every subsystem needs to update every tick.
- In games and scientific codes alike, expensive fields are often updated at lower frequency than entity movement or occupancy.

Ferox mapping:

- Run some serial-core work every `N` ticks or on demand:
  - frontier telemetry
  - scent diffusion
  - full snapshot builds when no client requested a frame
  - recombination/division checks already do this partially

Practical experiment:

1. Add a perf-only mode where `simulation_update_scents()` runs every 2 ticks and decays proportionally.
2. Add a perf-only mode where frontier telemetry runs every 2 or 4 ticks.
3. Compare resulting component and broad perf numbers before deciding whether a gameplay-science compromise is acceptable.

Why it is attractive:

- Very high upside.
- Particularly strong if the expensive subsystem is observability-only or visually smoothable.

### 4. Interaction filtering and broadphase masks

Why:

- The Apple collision/contact material is effectively a reminder to avoid testing impossible interactions.
- Combat and possibly telemetry are still paying for scans that could be reduced by simple preclassification.

Ferox mapping:

- Add cheap per-tile or per-colony flags:
  - "has enemy-adjacent border"
  - "has mixed lineage frontier"
  - "can emit toxin"
- Skip `simulation_resolve_combat()` work in tiles that cannot contain border conflicts.

Practical experiment:

1. Build a per-tile border mask during spread/sync.
2. Run combat only on tiles flagged as mixed-neighbor or toxin-active.
3. Add a dedicated combat component benchmark scenario that guarantees border contention so the before/after is meaningful.

Why it is attractive:

- It converts expensive neighbor checks into cheap filtering.
- It matches both game broadphase and scientific sparse-update intuition.

### 5. Frontier lists instead of whole-grid telemetry scans

Why:

- `frontier_telemetry_compute()` is currently expensive enough to compete with the main field updates.
- Frontier metrics should not need to rediscover the frontier from scratch every time if spread/combat already touches those cells.

Ferox mapping:

- Maintain a frontier cell list or tile frontier counts incrementally during spread and sync.
- Recompute diversity/entropy from the maintained frontier data instead of full-grid scans.

Practical experiment:

1. Prototype an optional per-tick frontier list builder in `atomic_tick()` / `simulation_tick()`.
2. Feed that directly into telemetry code.
3. Compare list-maintenance cost against the current read-only scan.

Why it is attractive:

- Strong fit for the measured telemetry hotspot.
- Conceptually aligned with frontier-driven colony behavior already present in the model.

### 6. Quantized or fixed-point transport fields

Why:

- Nutrient, toxin, and scent fields are bandwidth-heavy.
- Old simulation and game techniques often trade precision for cache residency and vector-friendliness.

Ferox mapping:

- Replace `float` fields with quantized `uint16_t` or fixed-point buffers in a perf branch.
- Reserve float only where biologically necessary.

Practical experiment:

1. Add a compile-time experiment for quantized scent or nutrient storage.
2. Validate output drift with a statistical regression test, not exact snapshots.
3. Re-measure both component timings and broad simulation throughput.

Why it is attractive:

- Smaller arrays mean better cache behavior.
- Helps both serial loops and future SIMD.

Risk:

- Higher correctness/behavior risk than tiling or dirty-region work.

### 7. Temporal or wavefront pipelining for field updates

Why:

- Stencil papers and CPU slides both point toward reuse across time as well as space.
- Ferox runs several diffusion-like passes that may benefit from more structured scheduling.

Ferox mapping:

- Consider wavefront or blocked multi-step updates for nutrients/scents in a perf branch.
- This is especially relevant if fields begin to dominate after the simpler wins land.

Practical experiment:

1. First do spatial tiling.
2. Only if spatial tiling wins, try temporal blocking on a narrow benchmark.

Why it is attractive:

- Potentially large upside.

Risk:

- Higher implementation complexity.
- Harder to keep readable and maintainable.

## Recommended Next Experiments

In order:

1. Narrow raw-grid encode per-cell overhead now that decode-helper cleanup was tried and rejected on `test_perf_unit_protocol`.
2. Dirty-tile or frontier-list telemetry that pushes beyond the current one-pass rewrite.
3. Tiled stencil benchmark for `simulation_update_nutrients()` and the remaining transport-heavy paths.
4. Multirate telemetry and scent update experiments.
5. Combat broadphase extension from contested cells to tile-level mixed-frontier metadata if the combat hotspot grows again.

Implemented from this list already:

- The frontier telemetry path was partially addressed with a one-pass scan + cached lineage resolution rewrite.
- The field stencil path was partially addressed with a branch-reduced interior update.
- Combat broadphase now skips non-contested border cells and has a focused sparse-border perf test.
- Frontier telemetry now also uses direct-index lineage histograms instead of linear bucket scans.
- Snapshot building now uses a direct `colony_id -> proto_idx` lookup table during the grid pass instead of repeated protocol-list searches.
- Snapshot building now also maps `colony_id -> proto_index` directly during centroid accumulation, removing the extra `world_index` indirection from the grid pass.
- Snapshot building now also skips the redundant zero-fill in inline-grid allocation because the subsequent snapshot scan writes every grid cell unconditionally.
- `protocol_serialize_world_state()` now writes encoded inline-grid payloads directly into the final output buffer instead of staging them in a temporary `grid_buffer` first.
- Transport now has a no-biofilm fast path so the common zero-EPS case does not pay attenuation work in nutrient, toxin, and scent updates.

Rejected from this list so far:

- EPS caching inside `simulation_update_scents()` was not a win and was reverted.
- A per-colony scalar cache for nutrient/scent update math was not a robust cross-machine win and was reverted.
- A helper-based raw-grid decode cleanup in `protocol_deserialize_grid_rle()` regressed `test_perf_unit_protocol` noisy-grid medians and was reverted.
- Structured spawn-feedback wiring (`MSG_ACK`/`MSG_ERROR`) is now no longer a recommended next experiment because the protocol/client correctness slice has been implemented.
- Selection-feedback wiring is also no longer a recommended next experiment because `CMD_SELECT_COLONY` now uses the same immediate command-status surface.

## What Not To Chase First

- More micro-optimizing of atomic phase dispatch. The serial core is still the bigger problem.
- Exotic lock-free control tricks for the atomic runner. The current bottlenecks are not there anymore.
- Protocol delta encoding before dirty-region metadata exists. The same metadata is likely needed for both transport and telemetry wins.
