# Colony Intelligence

This document captures both the current Ferox colony intelligence model and the
target vision for a smarter, more explainable colony behavior system.

## What Is In Place Now

Ferox already has a strong foundation for smarter colonies.

- `Genome` includes sensing, signaling, aggression, resilience, dormancy,
  biofilm, motility, specialization, learning, memory, and placeholder
  `hidden_weights` fields in `src/shared/types.h`.
- `Colony` includes live runtime state such as stress, dormancy, biofilm,
  signal strength, drift, and directional success history in `src/shared/types.h`.
- The live atomic runtime already reacts to nutrient, toxin, signal, and alarm
  layers, plus dormancy, biofilm, motility drift, specialization, and social
  sensing in `src/server/atomic_sim.c`.
- Colony dynamics already update stress, state transitions, biofilm, signal
  output, drift, and learning-related history in `src/server/simulation.c`.
- Horizontal gene transfer, mutation, recombination, and genetic distance are
  already present in `src/server/genetics.c`.

## First Graph Pass

Ferox now has a first explicit per-colony behavior graph pass.

- it runs once per colony inside `simulation_update_colony_dynamics()`
- it senses nutrients, toxins, alarm pressure, friendly signal, border pressure,
  frontier ratio, population trend, and directional memory
- it converts those inputs into weighted `drives` for growth, caution,
  hostility, cohesion, exploration, and preservation
- those drives feed action heads for expand, attack, defend, signal, transfer,
  dormancy, and motility
- the current dominant mode, focus direction, top sensors, and top drives are
  cached on each colony for runtime use and UI display
- selected-colony detail now also surfaces the strongest active sensor -> drive
  link and drive -> action link so the current decision chain is explainable
- the selected-colony sheet now carries the full action family, including
  transfer, dormancy, and motility outputs
- selected-colony panels now also show top-ranked actions plus second-ranked
  sensor -> drive and drive -> action links

This graph is now genetically encoded with explicit sensor gains, drive biases,
drive weights, action biases, and action weights inside `Genome`. The older
`hidden_weights[8]` fields are still present as legacy compatibility genes, but
the runtime now uses the explicit graph tables.

The active atomic runtime now also runs serial combat maintenance using the same
graph-shaped action outputs, so the default live server no longer limits richer
conflict behavior to the legacy simulation path.

## Current Limits

Ferox does not yet have one explicit colony “brain”. Instead, intelligence is
still spread across many scalar heuristics.

- the graph is still fixed-topology; Ferox does not yet support a free-form
  evolvable edge list or node set.
- learning is shallow: mostly directional success history rather than a full
  sensing -> memory -> decision model.
- the live atomic path still does not express a fully unified strategic layer
  for expand / defend / attack / merge / dormancy choices.
- clients only recently gained richer selected-colony stats; they still do not
  render a true behavior graph yet.

## Target Vision

The best fit for Ferox is a fixed-topology weighted behavior graph that runs per
colony, not per cell.

Recommended flow:

1. `Sensors`
   - nutrients, toxins, free space, edge pressure, alarm intensity, kin signal,
     enemy pressure, growth trend, fragmentation risk, stress
2. `Drives`
   - growth, fear, hostility, cohesion, curiosity, preservation, opportunism
3. `Regulators / memory`
   - quorum gate, stress amplifier, frontier focus, memory echo, dormancy gate
4. `Action heads`
   - expand, attack, defend, signal, alarm, merge, gene transfer, dormancy,
     biofilm, motility

The graph should output:

- scalar intents used by runtime systems
- directional intent that biases the existing 8-direction spread model
- a compact human-readable summary for CLI/GUI display

## Genetic Model

The graph should be genetically encoded and evolvable.

- weights and biases determine response strength
- mutation nudges weights, gains, decay, and optional edge enables
- recombination should happen by behavior module, not blind averaging
- horizontal gene transfer should copy a small themed parameter bundle, not just
  a random scalar trait

## Character Archetypes

The same graph can produce different colony personalities.

- aggressive: strong threat -> attack routing
- peaceful: stronger kinship -> signal / merge / restraint
- defensive: high fortify, biofilm, alarm behavior
- exploratory: strong curiosity and frontier seeking
- cooperative: high signal, merge, and gene-transfer tendencies
- opportunistic: attacks weak borders and open terrain quickly

## UI Goal

When tabbing through colonies in the terminal or GUI, users should see both the
current runtime state and the colony’s “character”.

Current selected-colony detail now exposes:

- state
- behavior mode
- focus direction
- dominant sensor
- dominant drive
- secondary sensor
- secondary drive
- strongest sensor -> drive link
- strongest drive -> action link
- second-ranked sensor -> drive link
- second-ranked drive -> action link
- transfer / dormancy / motility action outputs
- stress
- biofilm
- signal
- drift
- live action outputs for expand, attack, defend, and signal
- character summaries for expansion, aggression, resilience, cooperation,
  efficiency, and learning

Future selected-colony detail should also expose:

- full ranked drives
- full ranked sensor inputs
- more than one weighted edge per layer
- current action outputs
- preferred expansion direction

## Active Tracking

- `#93` Define a first-class colony behavior graph for smarter decisions
- `#94` Surface colony state and behavior stats in protocol, CLI, and GUI
- `#92` Restore colony ecology behavior parity on the atomic runtime path
- `#87` Implement signal, alarm, and horizontal gene-transfer behaviors
- `#101` Wire Monod uptake and EPS-dependent transport into the live runtime
- `#102` Replace heuristic contact HGT with plasmid kinetics and metrics
- `#103` Split atomic serial maintenance cadence by freshness requirements
- `#104` Unify neighborhood topology across spread, division, combat, and frontier telemetry
- `#105` Derive quorum state from signal field, memory, and thresholds
