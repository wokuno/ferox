# Lab Notebook: HGT Unit Test Coverage

- **Run label**: `20260324-233047-hgt-unit-test-coverage`
- **Date**: 2026-03-24T23:30:47Z
- **Area**: Genetics / Correctness Testing

## Starting Context

- HEAD: `ed494b4` (fix: report colony selection outcomes immediately)
- Prior run: `20260324-181517-selection-command-feedback` — added MSG_ACK/MSG_ERROR for CMD_SELECT_COLONY
- Prior recommendation: generalize command-status to CMD_RESET or speed-limit clamping
- Unrelated local edits in docs/scripts/go files; will not revert

## Handoff Recommendation — Rejected

The prior handoff recommends extending structured command feedback to CMD_RESET.
I reject this because fresh research reveals a higher-impact gap: `genome_transfer_genes()`
has **zero unit tests** and only one integration test in test_simulation_logic.c that checks
a single trait (toxin_resistance). HGT is a core genetic mechanic with 6 independent
probabilistic transfer paths, any of which could hide bugs in the blending math.
Filling this correctness gap is more valuable than incremental protocol feedback expansion.

## Research Topics (28 topics, 8 areas)

### Simulation Mechanics (6 topics)
1. **Mutation mechanics** — Fully implemented, multi-level (MUTATE_FIELD/MUTATE_FIELD_SLOW), 20+ mutated genes. No issues found. (genetics.c:206-305)
2. **HGT implementation** — Real, via genome_transfer_genes() at borders. 6 random decisions per call: toxin_resistance(30%), nutrient_sensitivity(30%), efficiency(20%), dormancy_resistance(20%), behavior drive(35%), behavior action(35%). (genetics.c:502-538)
3. **Quorum sensing** — Real mechanic with 3-cell radius, modulates spread and dormancy. (simulation_common.c:11-82)
4. **Combat system** — Sophisticated 2-pass (toxin emission + resolution), strategic modifiers (flanking, size, stress). (simulation.c:1869-2100)
5. **Colony division** — Fully functional via connected-component detection, rate-limited to 1/tick. (simulation.c:730-832)
6. **Environmental layers** — All 4 layers (nutrients, toxins, signals, alarms) are actively simulated with diffusion, decay, and source tracking. (simulation.c:1115-1430)

### Ecology & Colony States (5 topics)
7. **Dormancy/persister states** — Full implementation with entry/exit conditions, stress-driven. (simulation.c:1620-1635, simulation_common.c:228-276)
8. **Colony death** — Multi-factorial: starvation, toxin damage, natural decay, senescence. Completely untested. (simulation.c:2170-2240)
9. **Biofilm** — Real mechanic affecting toxin damage, nutrient depletion, death rates, combat defense, stress. (simulation.c:1584-1595)
10. **Colony state transitions** — 3-state machine (NORMAL/DORMANT/STRESSED), partially tested. (types.h:149-151)
11. **Mutualism** — Not implemented; HGT and signals exist but no reciprocal benefit systems.

### Protocol & Transport (4 topics)
12. **Command feedback completeness** — Only CMD_SPAWN_COLONY and CMD_SELECT_COLONY have ACK/ERROR; pause/resume/speed/reset don't. (server.c:214-330)
13. **Delta protocol** — MSG_WORLD_DELTA exists but is chunking, not true diffs. Full grid sent every tick. (protocol.c:568-666)
14. **Snapshot serialization** — 14-byte header, RLE or raw grid, 76 bytes/colony. RLE sampling only checks first 4096 cells. (protocol.c:434-566)
15. **Reconnection** — Not supported; one-shot protocol with no session persistence.

### Testing & Correctness (5 topics)
16. **Test coverage gaps** — Death mechanics completely untested; HGT has no unit tests; state transitions partially tested. 27 CTest targets exist.
17. **Genetics test coverage** — 21 tests covering mutation, distance, merge, compatibility, creation, drift, colors. No HGT, no recombination unit tests.
18. **Simulation tick phase coverage** — 7 phases documented; age/spread/combat tested; nutrients/signals sparsely tested.
19. **Science benchmarks** — 6 scenarios defined in config/science_benchmarks.json but not validated as runnable.
20. **Visual stability** — 17 tests across 5 scale categories; all passing.

### Architecture & Scaling (3 topics)
21. **Atomic tick design** — Parallel lock-free path with double-buffered grid, CAS-based claims, periodic serial maintenance.
22. **Threadpool** — Single global queue with worker-local state, range-based or interleaved scheduling, 4096 task freelist per worker.
23. **Hardware detection** — Auto-tuning for CPU/Apple/AMD targets with profile-specific serial intervals and frontier thresholds.

### Client & UX (3 topics)
24. **Terminal renderer** — ANSI-based with procedural shapes, info panel, colony detail with 18+ metrics.
25. **GUI renderer** — SDL2-based with zoom (1-50x), grid overlay, click selection, antialiased lines, bitmap font.
26. **feroxclub dashboard** — Go-based local web app at :8787 with live monitoring, lab notebook browsing.

### Build & Tooling (2 topics)
27. **Build system** — CMake with C11, Debug/Release, optional sanitizers/coverage. 30+ test executables.
28. **CI** — No workflow files; only CODEOWNERS, issue templates, PR template.

## Candidate Experiment Ideas

1. **Add HGT unit tests** — Fill zero-coverage gap for genome_transfer_genes(). Tests all 6 probabilistic branches + edge cases (null, zero strength, full strength). Impact: high (core genetic mechanic untested). Effort: low. Measurability: pass/fail. Reversibility: pure test addition.

2. **Add colony death/extinction tests** — Death mechanics are completely untested. Impact: high. Effort: medium (need world setup). Measurability: pass/fail. Reversibility: pure test addition.

3. **CMD_RESET structured feedback** — Handoff recommendation. Impact: medium. Effort: medium (server + client). Measurability: pass/fail. Reversibility: easy.

4. **Dormancy exit test coverage** — Entry tested, exit not. Impact: medium. Effort: low. Measurability: pass/fail. Reversibility: easy.

5. **Science benchmark validation** — Verify config/science_benchmarks.json scenarios are actually runnable. Impact: medium. Effort: high. Measurability: run or don't. Reversibility: easy.

## Experiment Selection Reasoning

- **HGT unit tests** wins because: (a) genome_transfer_genes() is a core function with zero unit tests, (b) it has complex probabilistic logic (6 independent random decisions + blending math) that could hide bugs, (c) the only integration test checks one trait, (d) tests are low-effort, high-value, and purely additive.
- **Colony death tests** is runner-up but requires more world scaffolding.
- **CMD_RESET feedback** is incremental UX, less critical than correctness gaps.

## Hypothesis

If we add unit tests covering all 6 probabilistic transfer paths of genome_transfer_genes() plus edge cases (null inputs, zero/full transfer strength, trait convergence direction), then we will either confirm the function's correctness or discover bugs in the blending math that could cause silent trait corruption during HGT events.

## Implementation Plan

Add tests to test_genetics_advanced.c in a new "Horizontal Gene Transfer Tests" section:
1. `hgt_null_recipient_is_noop` — null recipient doesn't crash
2. `hgt_null_donor_is_noop` — null donor doesn't crash
3. `hgt_zero_strength_is_noop` — transfer_strength=0 causes no change
4. `hgt_negative_strength_is_noop` — negative transfer_strength causes no change
5. `hgt_full_strength_copies_donor_traits` — transfer_strength=1.0 with seeded RNG copies all eligible traits exactly
6. `hgt_partial_strength_blends_toward_donor` — transfer_strength=0.5 moves traits halfway toward donor
7. `hgt_transfers_converge_recipient_toward_donor` — repeated transfers converge traits
8. `hgt_behavior_drive_transfer_updates_weights` — drive bias + weights transferred together
9. `hgt_behavior_action_transfer_updates_weights` — action bias + weights transferred together

## Implementation Changes

See diff below — all changes in tests/test_genetics_advanced.c only.

## Test Commands

```bash
cmake --build build -j4
ctest --test-dir build --output-on-failure -R GeneticsAdvancedTests
```

## Results

Tests ran 3 times, all stable:
- Run 1: 29 passed, 0 failed
- Run 2: 29 passed, 0 failed
- Run 3: 29 passed, 0 failed

All 9 new HGT tests pass. The broader suite (Phase1-6, Genetics, Simulation, World) shows
only a pre-existing Phase5 failure in test_server_handles_pause_resume_commands (colony
selection test, unrelated to genetics changes — test_phase5.c is unmodified).

## Interpretation

**Hypothesis confirmed**: genome_transfer_genes() is correct across all 6 probabilistic
transfer paths. The blending math `recipient += (donor - recipient) * strength` works
correctly for:
- All 4 scalar traits (toxin_resistance, nutrient_sensitivity, efficiency, dormancy_resistance)
- Behavior drive biases + weights (transferred atomically per drive)
- Behavior action biases + weights (transferred atomically per action)

Edge cases are also handled correctly:
- Null recipient/donor → early return, no crash
- Zero or negative strength → no-op, no trait changes
- Full strength (1.0) → trait becomes exactly the donor value
- Partial strength (0.5) → trait blends halfway
- Repeated transfers → convergence toward donor genome

No bugs discovered. The function is well-implemented.

## Next Recommended Experiment

Add unit tests for colony death mechanics (starvation, toxin damage, natural decay,
senescence). Death is completely untested despite being a multi-factorial system with
4 independent triggers. This is the next-highest correctness gap after HGT.
