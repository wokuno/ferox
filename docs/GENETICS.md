# Ferox Genetics System

This document explains the genetic system that drives colony behavior and evolution in Ferox.

## Overview

Every bacterial colony in Ferox has a **genome** - a set of genetic parameters that determine its appearance and behavior. These genomes mutate over time and can be merged during colony recombination, leading to emergent evolutionary dynamics.

## Colony Decision Model (AI Architecture)

The genetic system functions as a **neural-network-like decision model** where genome traits act as weights that transform environmental inputs into behavioral outputs. Each colony's genome encodes a unique "personality" that determines how it responds to its environment.

Current implementation note:

- Ferox now stores an explicit fixed-topology behavior graph in the genome using
  sensor gains, drive biases, drive weights, action biases, and action weights.
- The runtime evaluates that graph once per colony, not per cell.
- Legacy `hidden_weights[8]` still exist for compatibility, but the live colony
  controller now uses the explicit graph tables.

### System Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                     FEROX COLONY DECISION MODEL                             │
│               (Genetic Algorithm + Neural-Network-Like System)              │
└─────────────────────────────────────────────────────────────────────────────┘

╔═══════════════════════════════════════════════════════════════════════════╗
║                              INPUTS (Sensors)                              ║
╠═══════════════════════════════════════════════════════════════════════════╣
║                                                                           ║
║  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐           ║
║  │  NEIGHBORS (8)  │  │   ENVIRONMENT   │  │   INTERNAL      │           ║
║  │                 │  │                 │  │                 │           ║
║  │ • Empty cells   │  │ • Nutrients     │  │ • Cell age      │           ║
║  │ • Enemy cells   │  │ • Toxins        │  │ • Stress level  │           ║
║  │ • Allied cells  │  │ • Signals       │  │ • Colony state  │           ║
║  │ • Border status │  │ • Edge distance │  │ • Population    │           ║
║  │                 │  │ • Density       │  │                 │           ║
║  └────────┬────────┘  └────────┬────────┘  └────────┬────────┘           ║
║           │                    │                    │                     ║
╚═══════════│════════════════════│════════════════════│═════════════════════╝
            │                    │                    │
            ▼                    ▼                    ▼
╔═══════════════════════════════════════════════════════════════════════════╗
║                     GENOME WEIGHTS (Neural "Weights")                      ║
╠═══════════════════════════════════════════════════════════════════════════╣
║                                                                           ║
║    ┌───────────────────────────────────────────────────────────────┐     ║
║    │                    SPREAD WEIGHTS [8]                          │     ║
║    │         (Direction preference - like attention heads)          │     ║
║    │                                                                 │     ║
║    │                         N [0.7]                                 │     ║
║    │                           ↑                                     │     ║
║    │                    NW [0.4]  NE [0.5]                          │     ║
║    │                       ↖   ↑   ↗                                │     ║
║    │               W [0.3] ←   ●   → E [0.8]                        │     ║
║    │                       ↙   ↓   ↘                                │     ║
║    │                    SW [0.6]  SE [0.9]                          │     ║
║    │                           ↓                                     │     ║
║    │                         S [0.5]                                 │     ║
║    └───────────────────────────────────────────────────────────────┘     ║
║                                                                           ║
║    ┌────────────────┐ ┌────────────────┐ ┌────────────────┐              ║
║    │ BASIC TRAITS   │ │ SOCIAL TRAITS  │ │ SURVIVAL       │              ║
║    │                │ │                │ │                │              ║
║    │ spread_rate    │ │ social_factor  │ │ resilience     │              ║
║    │ metabolism     │ │ detection_range│ │ dormancy_thresh│              ║
║    │ aggression     │ │ merge_affinity │ │ biofilm_invest │              ║
║    │ mutation_rate  │ │ signal_emiss.  │ │ toxin_resist.  │              ║
║    │ efficiency     │ │ signal_sensit. │ │ motility       │              ║
║    └───────┬────────┘ └───────┬────────┘ └───────┬────────┘              ║
║            │                  │                  │                        ║
╚════════════│══════════════════│══════════════════│════════════════════════╝
             │                  │                  │
             ▼                  ▼                  ▼
╔═══════════════════════════════════════════════════════════════════════════╗
║                      DECISION COMPUTATION                                  ║
╠═══════════════════════════════════════════════════════════════════════════╣
║                                                                           ║
║  ┌─────────────────────────────────────────────────────────────────────┐ ║
║  │                    SPREAD DECISION                                   │ ║
║  │                                                                      │ ║
║  │   P(spread) = spread_rate × metabolism × direction_weight           │ ║
║  │                                                                      │ ║
║  │   Direction selected by: max(spread_weights[d] × nutrient_gradient  │ ║
║  │                              × social_influence × density_factor)    │ ║
║  └─────────────────────────────────────────────────────────────────────┘ ║
║                                                                           ║
║  ┌─────────────────────────────────────────────────────────────────────┐ ║
║  │                    COMBAT DECISION                                   │ ║
║  │                                                                      │ ║
║  │   P(overtake) = aggression × (1.0 - enemy_resilience)               │ ║
║  │                                                                      │ ║
║  │   Attack occurs when: rand() < P(overtake)                          │ ║
║  └─────────────────────────────────────────────────────────────────────┘ ║
║                                                                           ║
║  ┌─────────────────────────────────────────────────────────────────────┐ ║
║  │                    MERGE DECISION                                    │ ║
║  │                                                                      │ ║
║  │   distance = genome_distance(self, neighbor)                        │ ║
║  │   threshold = 0.05 + avg(merge_affinity) × 0.1                      │ ║
║  │                                                                      │ ║
║  │   Merge when: distance ≤ threshold AND related(parent/sibling)      │ ║
║  └─────────────────────────────────────────────────────────────────────┘ ║
║                                                                           ║
╚═══════════════════════════════════════════════════════════════════════════╝
             │
             ▼
╔═══════════════════════════════════════════════════════════════════════════╗
║                         OUTPUTS (Actions)                                  ║
╠═══════════════════════════════════════════════════════════════════════════╣
║                                                                           ║
║  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   ║
║  │  SPREAD  │  │  MUTATE  │  │  DIVIDE  │  │ RECOMBINE│  │ TRANSFER │   ║
║  │          │  │          │  │          │  │          │  │  GENES   │   ║
║  │ Colonize │  │ Evolve   │  │ Split    │  │ Merge    │  │ Exchange │   ║
║  │ adjacent │  │ genome   │  │ into new │  │ with kin │  │ traits   │   ║
║  │ cells    │  │ traits   │  │ colonies │  │ colonies │  │ on touch │   ║
║  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘   ║
║       │             │             │             │             │          ║
╚═══════│═════════════│═════════════│═════════════│═════════════│══════════╝
        │             │             │             │             │
        └──────────┬──┴──────┬──────┴──────┬──────┴─────────────┘
                   │         │             │
                   ▼         ▼             ▼
╔═══════════════════════════════════════════════════════════════════════════╗
║                     FEEDBACK LOOPS (Evolution)                             ║
╠═══════════════════════════════════════════════════════════════════════════╣
║                                                                           ║
║  ┌─────────────────────────────────────────────────────────────────────┐ ║
║  │                                                                      │ ║
║  │   ┌─────────────┐     ┌─────────────┐     ┌─────────────┐          │ ║
║  │   │  MUTATION   │────▶│  SELECTION  │────▶│  FITNESS    │          │ ║
║  │   │             │     │             │     │             │          │ ║
║  │   │ δ = ±0.1    │     │ Colonies    │     │ High cells  │          │ ║
║  │   │ per trait   │     │ compete for │     │ = success   │          │ ║
║  │   │ per tick    │     │ territory   │     │ = reproduce │          │ ║
║  │   └─────────────┘     └─────────────┘     └──────┬──────┘          │ ║
║  │          ▲                                       │                  │ ║
║  │          │          ┌────────────────────────────┘                  │ ║
║  │          │          ▼                                               │ ║
║  │   ┌──────┴──────────────────┐     ┌─────────────┐                  │ ║
║  │   │     GENOME UPDATE       │     │ INHERITANCE │                  │ ║
║  │   │                         │◀────│             │                  │ ║
║  │   │ Traits drift toward     │     │ Division:   │                  │ ║
║  │   │ successful strategies   │     │ copy+mutate │                  │ ║
║  │   │                         │     │             │                  │ ║
║  │   │ mutation_rate itself    │     │ Merge:      │                  │ ║
║  │   │ can mutate (meta-evol)  │     │ weighted avg│                  │ ║
║  │   └─────────────────────────┘     └─────────────┘                  │ ║
║  │                                                                      │ ║
║  └─────────────────────────────────────────────────────────────────────┘ ║
║                                                                           ║
╚═══════════════════════════════════════════════════════════════════════════╝
```

### The Neural Network Analogy

The genome functions similarly to a neural network:

| Neural Network Concept | Ferox Equivalent |
|------------------------|------------------|
| **Input neurons** | Neighbor cells, environment sensors |
| **Weights** | Genome traits (spread_weights, aggression, etc.) |
| **Activation function** | Probability thresholds + random sampling |
| **Output neurons** | Actions (spread, attack, merge, divide) |
| **Backpropagation** | Natural selection (successful colonies survive) |
| **Learning rate** | `mutation_rate` (self-modifying!) |
| **Attention mechanism** | `spread_weights[8]` - direction preferences |

### Genome → Behavior → Evolution Flow

```
┌────────────────────────────────────────────────────────────────────────────┐
│                     EVOLUTIONARY CYCLE (per tick)                          │
└────────────────────────────────────────────────────────────────────────────┘

   TICK N                    TICK N+1                    TICK N+2
     │                          │                          │
     ▼                          ▼                          ▼
┌─────────┐              ┌─────────┐              ┌─────────┐
│ GENOME  │              │ GENOME' │              │ GENOME''│
│         │──mutation──▶ │         │──mutation──▶ │         │
│ traits  │              │ traits  │              │ traits  │
└────┬────┘              └────┬────┘              └────┬────┘
     │                        │                        │
     │ encodes                │ encodes                │ encodes
     ▼                        ▼                        ▼
┌─────────┐              ┌─────────┐              ┌─────────┐
│BEHAVIOR │              │BEHAVIOR │              │BEHAVIOR │
│         │              │         │              │         │
│ spread  │              │ spread  │              │ spread  │
│ attack  │              │ attack  │              │ attack  │
│ merge   │              │ merge   │              │ merge   │
└────┬────┘              └────┬────┘              └────┬────┘
     │                        │                        │
     │ results in             │ results in             │ results in
     ▼                        ▼                        ▼
┌─────────┐              ┌─────────┐              ┌─────────┐
│TERRITORY│              │TERRITORY│              │TERRITORY│
│         │              │         │              │         │
│ cells   │──survival──▶ │ cells   │──survival──▶ │ cells   │
│ count   │              │ count   │              │ count   │
└─────────┘              └─────────┘              └─────────┘
     │                        │                        │
     │                   ┌────┴────┐                   │
     │                   │DIVISION?│                   │
     │                   └────┬────┘                   │
     │                        │                        │
     │              ┌─────────┴─────────┐              │
     │              ▼                   ▼              │
     │        ┌─────────┐         ┌─────────┐         │
     │        │ CHILD A │         │ CHILD B │         │
     │        │ mutated │         │ mutated │         │
     │        │ genome  │         │ genome  │         │
     │        └─────────┘         └─────────┘         │
     │                                                 │
     └─────────────────────────────────────────────────┘
                     (lineage continues)
```

### Simulation Tick Phases

Each simulation tick executes these phases in order:

```
simulation_tick(world)
        │
        ▼
┌───────────────────┐
│  1. AGE CELLS     │  All cells age += 1
└─────────┬─────────┘
          │
          ▼
┌───────────────────┐
│  2. SPREAD        │  Each colony tries to expand
│                   │
│  for each cell:   │  ┌──────────────────────────────────┐
│    for each dir:  │  │ P = spread_rate × metabolism     │
│      if empty:    │──│ if rand() < P: colonize          │
│      if enemy:    │  │ P = aggression × (1-resilience)  │
│                   │  │ if rand() < P: overtake          │
│                   │  └──────────────────────────────────┘
└─────────┬─────────┘
          │
          ▼
┌───────────────────┐
│  3. MUTATE        │  Each active colony mutates genome
│                   │
│  for each trait:  │  ┌──────────────────────────────────┐
│    P = mut_rate   │  │ if rand() < mutation_rate:       │
│    if rand() < P: │──│   trait += random(±0.1)          │
│                   │  │   clamp to valid range           │
│                   │  └──────────────────────────────────┘
└─────────┬─────────┘
          │
          ▼
┌───────────────────┐
│  4. DIVISIONS     │  Check for colony splits
│                   │
│  flood_fill each  │  ┌──────────────────────────────────┐
│  colony to find   │──│ if components > 1:               │
│  disconnected     │  │   largest keeps ID               │
│  components       │  │   others become new colonies     │
│                   │  │   with mutated genomes           │
│                   │  └──────────────────────────────────┘
└─────────┬─────────┘
          │
          ▼
┌───────────────────┐
│  5. RECOMBINE     │  Check for compatible merges
│                   │
│  for adjacent     │  ┌──────────────────────────────────┐
│  colony pairs:    │──│ if related AND distance < 0.05:  │
│    check compat.  │  │   merge genomes (weighted avg)   │
│                   │  │   larger absorbs smaller         │
│                   │  └──────────────────────────────────┘
└─────────┬─────────┘
          │
          ▼
┌───────────────────┐
│  6. UPDATE STATS  │  Recount cells, update max_pop
└─────────┬─────────┘
          │
          ▼
     tick++
```

### Genetic Algorithm Properties

The Ferox genetics system implements a **genetic algorithm** with these key properties:

```
┌────────────────────────────────────────────────────────────────────────────┐
│                    GENETIC ALGORITHM COMPONENTS                            │
├────────────────────────────────────────────────────────────────────────────┤
│                                                                            │
│  ┌─────────────────────────────────────────────────────────────────────┐  │
│  │ CHROMOSOME: Genome struct                                            │  │
│  │                                                                      │  │
│  │ ┌────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┬────┐       │  │
│  │ │spr │mut │agg │res │met │det │soc │mrg │nut │edg │den │... │ ...   │  │
│  │ │rate│rate│    │    │    │rng │fac │aff │sns │aff │tol │    │       │  │
│  │ └────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┴────┘       │  │
│  │                     ~25 float genes + color genes                    │  │
│  └─────────────────────────────────────────────────────────────────────┘  │
│                                                                            │
│  ┌─────────────────────────────────────────────────────────────────────┐  │
│  │ FITNESS FUNCTION: Territory control                                  │  │
│  │                                                                      │  │
│  │   fitness = colony.cell_count / world.total_cells                   │  │
│  │                                                                      │  │
│  │   Implicit selection: colonies with more cells have more            │  │
│  │   opportunities to spread and divide (reproduce)                    │  │
│  └─────────────────────────────────────────────────────────────────────┘  │
│                                                                            │
│  ┌─────────────────────────────────────────────────────────────────────┐  │
│  │ SELECTION: Spatial competition                                       │  │
│  │                                                                      │  │
│  │   • Colonies fight for territory (aggression vs resilience)         │  │
│  │   • Empty space is colonized by fastest spreaders                   │  │
│  │   • Colonies with 0 cells die (selection pressure)                  │  │
│  └─────────────────────────────────────────────────────────────────────┘  │
│                                                                            │
│  ┌─────────────────────────────────────────────────────────────────────┐  │
│  │ MUTATION: Per-tick genome drift                                      │  │
│  │                                                                      │  │
│  │   • Each gene: P(mutate) = mutation_rate                            │  │
│  │   • Mutation delta: ±0.1 (uniform random)                           │  │
│  │   • Self-referential: mutation_rate can mutate                      │  │
│  │   • Clamped to valid ranges                                         │  │
│  └─────────────────────────────────────────────────────────────────────┘  │
│                                                                            │
│  ┌─────────────────────────────────────────────────────────────────────┐  │
│  │ CROSSOVER: Genome merging during recombination                       │  │
│  │                                                                      │  │
│  │   child_trait = parent_a.trait × weight_a + parent_b.trait × weight_b│  │
│  │                                                                      │  │
│  │   where weight_x = cell_count_x / total_cells                       │  │
│  │                                                                      │  │
│  │   (Weighted averaging instead of traditional crossover)             │  │
│  └─────────────────────────────────────────────────────────────────────┘  │
│                                                                            │
│  ┌─────────────────────────────────────────────────────────────────────┐  │
│  │ SPECIATION: Geographic isolation                                     │  │
│  │                                                                      │  │
│  │   • Colony disconnection triggers division                          │  │
│  │   • Child colonies immediately mutate (divergence)                  │  │
│  │   • parent_id tracks lineage                                        │  │
│  │   • Related colonies can recombine if genetically similar           │  │
│  └─────────────────────────────────────────────────────────────────────┘  │
│                                                                            │
└────────────────────────────────────────────────────────────────────────────┘
```

### Horizontal Gene Transfer

In addition to vertical inheritance (parent→child), Ferox models **mechanistic HGT kinetics** with donor/recipient/transconjugant dynamics.

Each colony tracks a plasmid-like carrier fraction:

- `hgt_plasmid_fraction` (0..1): fraction of cells in the colony carrying transferable material.
- `hgt_is_transconjugant`: whether the colony became a donor through prior HGT.

On donor-recipient contact, transfer follows mass-action style kinetics:

```
P(transfer) = contact_rate
              × donor_rate(class)
              × recipient_uptake_rate
              × donor.gene_transfer_rate
              × donor_plasmid_fraction
              × (1 - recipient_plasmid_fraction)

recipient_plasmid_fraction += transfer_efficiency
                              × donor_plasmid_fraction
                              × (1 - recipient_plasmid_fraction)
```

Where `donor_rate(class)` is selected from `donor_transfer_rate` or `transconjugant_transfer_rate`.

Optional plasmid dynamics:

- **Cost (optional):** `hgt_fitness_scale = 1 - plasmid_cost_per_fraction × hgt_plasmid_fraction`; this scales spread pressure.
- **Loss (optional):** `hgt_plasmid_fraction -= plasmid_loss_rate × hgt_plasmid_fraction` each kinetics update.

Both behaviors are runtime configurable via `World.hgt_kinetics` (or `world_set_hgt_kinetics()`).

Instrumentation is exposed through `World.hgt_metrics` and per-colony counters:

- transfer totals
- transconjugant emergence events
- plasmid loss events
- cross-lineage transfer events

## Genome Structure

```c
typedef struct {
    float spread_weights[8];  // Direction preferences (N,NE,E,SE,S,SW,W,NW)
    float spread_rate;        // Overall spread probability (0-1)
    float mutation_rate;      // How often mutations occur (0-0.1)
    float aggression;         // Attack strength (0-1)
    float resilience;         // Defense strength (0-1)
    float metabolism;         // Growth speed modifier (0-1)
    
    // Social behavior (chemotaxis-like)
    float detection_range;    // How far to detect neighbors (0.1-0.5, % of world size)
    uint8_t max_tracked;      // Max neighbor colonies to track (1-4)
    float social_factor;      // -1 to +1: negative=repelled, positive=attracted
    float merge_affinity;     // 0-0.3: bonus to merge compatibility threshold
    
    // Environmental Sensing
    float nutrient_sensitivity;  // Response strength to nutrient gradients (0-1)
    float edge_affinity;         // Preference for world edges vs center (-1 to +1)
    float density_tolerance;     // Tolerance for crowded areas (0-1)
    
    // Colony Interactions
    float toxin_production;      // Rate of toxin secretion (0-1)
    float toxin_resistance;      // Resistance to environmental toxins (0-1)
    float signal_emission;       // Rate of chemical signal emission (0-1)
    float signal_sensitivity;    // Response to chemical signals (0-1)
    float gene_transfer_rate;    // Horizontal gene transfer probability (0-0.1)
    
    // Survival Strategies
    float dormancy_threshold;    // Stress level to trigger dormancy (0-1)
    float dormancy_resistance;   // Resistance to entering dormancy (0-1)
    float biofilm_investment;    // Resources invested in biofilm (0-1)
    float motility;              // Cell movement speed (0-1)
    float motility_direction;    // Preferred movement direction (0-2π)
    
    // Metabolic
    float efficiency;            // Metabolic efficiency (0-1)
    
    Color body_color;         // Interior cell color (RGB)
    Color border_color;       // Border cell color (RGB)
} Genome;
```

## Gene Descriptions

Runtime note:

- `signal_emission` / `signal_sensitivity` now drive active territory and alarm gradients.
- `gene_transfer_rate` now affects contact-driven horizontal gene transfer.
- `dormancy_resistance`, `motility`, and `motility_direction` now contribute to
  active colony state and directional drift on the atomic runtime path.

### Spread Weights (8 floats, 0-1)

Controls **directional preference** for colony growth.

```
        N (0)
    NW (7)  NE (1)
  W (6)  ●  E (2)
    SW (5)  SE (3)
        S (4)
```

| Index | Direction | Effect |
|-------|-----------|--------|
| 0 | North | Tendency to grow upward |
| 1 | Northeast | Tendency to grow up-right |
| 2 | East | Tendency to grow rightward |
| 3 | Southeast | Tendency to grow down-right |
| 4 | South | Tendency to grow downward |
| 5 | Southwest | Tendency to grow down-left |
| 6 | West | Tendency to grow leftward |
| 7 | Northwest | Tendency to grow up-left |

**Examples:**
- High N (0) + S (4): Vertical spreading tendency
- High NE (1) + SW (5): Diagonal growth pattern
- All equal (~0.5): Radial expansion

**Note:** Current implementation uses 4-connectivity spreading (N, E, S, W only), but spread_weights are defined for 8 directions for future expansion.

---

### Spread Rate (float, 0-1)

The **base probability** of spreading to an adjacent empty cell per tick.

| Value | Behavior |
|-------|----------|
| 0.0 | Never spreads (static colony) |
| 0.2 | Slow, cautious expansion |
| 0.5 | Moderate growth rate |
| 0.8 | Aggressive expansion |
| 1.0 | Maximum spread rate |

**Effective spread probability:**
```c
float effective = spread_rate × metabolism
```

---

### Mutation Rate (float, 0-0.1)

Controls the **probability** of each gene mutating per tick.

| Value | Mutations per ~100 ticks |
|-------|--------------------------|
| 0.01 | ~1 mutation |
| 0.05 | ~5 mutations |
| 0.10 | ~10 mutations |

**Self-referential:** The mutation rate itself can mutate, leading to interesting dynamics:
- High mutation colonies evolve rapidly but may lose optimal traits
- Low mutation colonies are stable but adapt slowly

---

### Aggression (float, 0-1)

Determines ability to **overtake enemy cells**.

When a colony cell is adjacent to an enemy cell:
```c
float attack_success = attacker->aggression × (1.0 - defender->resilience)
if (rand_float() < attack_success) {
    // Cell is captured
}
```

| Value | Behavior |
|-------|----------|
| 0.0 | Completely passive (never attacks) |
| 0.3 | Defensive, occasional attacks |
| 0.5 | Balanced aggression |
| 0.7 | Aggressive expansion |
| 1.0 | Maximum aggression |

**Trade-off:** High aggression enables territory capture but spreads resources thin.

---

### Resilience (float, 0-1)

Determines **defense** against enemy attacks.

```c
float defense = 1.0 - resilience  // Probability of being captured
```

| Value | Survival Rate vs Aggressor=1.0 |
|-------|-------------------------------|
| 0.0 | 0% (always captured) |
| 0.5 | 50% survival |
| 0.9 | 90% survival |
| 1.0 | 100% (invincible) |

**Trade-off:** High resilience protects borders but doesn't help expansion.

---

### Metabolism (float, 0-1)

Modifies **growth speed** by scaling the effective spread rate.

```c
float effective_spread = spread_rate × metabolism
```

| Value | Effect |
|-------|--------|
| 0.0 | No growth (dormant) |
| 0.5 | Half speed growth |
| 1.0 | Full speed growth |

**Use case:** Metabolism creates variation even among colonies with identical spread_rate.

---

### Detection Range (float, 0.1-0.5)

Controls how far a colony can **detect neighboring colonies** for social behavior.

| Value | Detection Distance |
|-------|-------------------|
| 0.1 | 10% of world size (short range) |
| 0.3 | 30% of world size (medium range) |
| 0.5 | 50% of world size (long range) |

The colony uses sparse grid sampling within this range to detect other colonies during spreading.

**Trade-off:** Larger detection range consumes more computation but enables earlier social responses.

---

### Max Tracked (uint8_t, 1-4)

Maximum number of **neighbor colonies to track** for social calculations.

| Value | Behavior |
|-------|----------|
| 1 | Only reacts to nearest colony |
| 2 | Tracks up to 2 neighbors |
| 4 | Can respond to multiple nearby colonies |

**Note:** Current implementation uses only the nearest detected colony.

---

### Social Factor (float, -1.0 to +1.0)

Determines **attraction or repulsion** toward detected neighbor colonies.

| Value | Behavior |
|-------|----------|
| -1.0 | Maximum repulsion (spreads away from neighbors) |
| -0.5 | Moderate repulsion |
| 0.0 | Neutral (no directional bias) |
| +0.5 | Moderate attraction |
| +1.0 | Maximum attraction (spreads toward neighbors) |

**Effect on spreading:**
```c
// Spread probability multiplied by [0.5, 1.5] based on direction
// alignment with social influence vector
spread_probability *= social_influence_multiplier;
```

**Emergent behaviors:**
- Positive colonies cluster together
- Negative colonies spread into empty territories
- Mixed populations create dynamic tension

---

### Merge Affinity (float, 0.0-0.3)

Provides a **subtle bonus** to genetic compatibility threshold for recombination.

| Value | Effective Threshold Bonus |
|-------|--------------------------|
| 0.0 | No bonus (base threshold 0.2) |
| 0.15 | +0.075 threshold (moderate) |
| 0.3 | +0.15 threshold (maximum) |

**Calculation:**
```c
float bonus = (a->merge_affinity + b->merge_affinity) / 2.0f * 0.5f;
float effective_threshold = 0.2f + bonus;  // Range: 0.2 to 0.35
```

**Use case:** Colonies with high merge affinity are more likely to merge with genetically similar neighbors, enabling cooperative evolution strategies.

---

### Environmental Sensing Traits

#### Nutrient Sensitivity (float, 0-1)

Controls how strongly a colony responds to **nutrient gradients** in the environment.

| Value | Behavior |
|-------|----------|
| 0.0 | Ignores nutrients (spreads uniformly) |
| 0.5 | Moderate chemotaxis toward nutrients |
| 1.0 | Strongly biased toward nutrient-rich areas |

**Effect:** Colonies spread preferentially toward cells with higher nutrient levels.

---

#### Edge Affinity (float, -1 to +1)

Determines preference for **world boundaries** vs interior.

| Value | Behavior |
|-------|----------|
| -1.0 | Avoids edges, prefers center |
| 0.0 | No edge preference |
| +1.0 | Prefers edges and corners |

---

#### Density Tolerance (float, 0-1)

Determines comfort level in **crowded areas**.

| Value | Behavior |
|-------|----------|
| 0.0 | Avoids dense colony clusters |
| 0.5 | Neutral to density |
| 1.0 | Thrives in crowded environments |

---

### Colony Interaction Traits

#### Toxin Production (float, 0-1)

Rate of **toxin secretion** into neighboring cells.

| Value | Effect |
|-------|--------|
| 0.0 | No toxin production |
| 0.5 | Moderate toxin output |
| 1.0 | Maximum toxin secretion |

**Trade-off:** Toxin production consumes metabolic resources.

---

#### Toxin Resistance (float, 0-1)

**Defense** against environmental toxins.

| Value | Survival in Toxic Environment |
|-------|------------------------------|
| 0.0 | Highly susceptible to toxins |
| 0.5 | Moderate resistance |
| 1.0 | Immune to toxin damage |

---

#### Signal Emission (float, 0-1)

Rate of **chemical signal** emission for colony communication.

| Value | Behavior |
|-------|----------|
| 0.0 | Silent (no signaling) |
| 0.5 | Moderate signal output |
| 1.0 | Strong signal broadcasting |

**Use case:** Colonies can coordinate behavior through chemical signals.

---

#### Signal Sensitivity (float, 0-1)

**Response strength** to chemical signals from other colonies.

| Value | Behavior |
|-------|----------|
| 0.0 | Ignores all signals |
| 0.5 | Moderate signal response |
| 1.0 | Highly responsive to signals |

---

#### Gene Transfer Rate (float, 0-0.1)

Probability of **horizontal gene transfer** when adjacent to another colony.

| Value | Transfers per ~100 contacts |
|-------|----------------------------|
| 0.01 | ~1 gene transfer event |
| 0.05 | ~5 gene transfer events |
| 0.10 | ~10 gene transfer events |

**Effect:** Enables colonies to acquire beneficial traits from neighbors.

---

### Survival Strategy Traits

#### Expensive-Trait Energetic Burden (derived)

The simulator applies explicit cost accounting to four high-impact traits:

`trait_load = toxin_production*0.24 + (biofilm_investment*biofilm_tendency)*0.20 + signal_emission*0.16 + motility*0.18`

- Higher `trait_load` reduces effective spread pressure.
- Higher `trait_load` increases nutrient maintenance demand and per-tick death pressure.
- This discourages universal all-max trait profiles and produces stable strategy tradeoffs.

---

#### Dormancy Threshold (float, 0-1)

**Stress level** that triggers transition to dormant state.

| Value | Behavior |
|-------|----------|
| 0.0 | Enters dormancy at slightest stress |
| 0.5 | Moderate stress tolerance before dormancy |
| 1.0 | Never enters dormancy (fights to death) |

---

#### Dormancy Resistance (float, 0-1)

**Resistance** to being forced into dormant state by external factors.

| Value | Behavior |
|-------|----------|
| 0.0 | Easily forced dormant |
| 0.5 | Moderate resistance |
| 1.0 | Cannot be forced dormant |

---

#### Biofilm Investment (float, 0-1)

Resources allocated to **biofilm formation**.

| Value | Effect |
|-------|--------|
| 0.0 | No biofilm (vulnerable but fast-spreading) |
| 0.5 | Moderate biofilm protection |
| 1.0 | Maximum biofilm (very defensive, slow growth) |

**Trade-off:** Biofilm increases resilience but reduces spread rate.

---

#### Motility (float, 0-1)

**Movement capability** of colony cells.

| Value | Behavior |
|-------|----------|
| 0.0 | Sessile (stationary cells) |
| 0.5 | Moderate cell movement |
| 1.0 | Highly motile cells |

---

#### Motility Direction (float, 0-2π)

**Preferred movement angle** for motile cells.

| Value | Direction |
|-------|-----------|
| 0 | East |
| π/2 | North |
| π | West |
| 3π/2 | South |

---

### Metabolic Traits

#### Efficiency (float, 0-1)

**Metabolic efficiency** - how well the colony converts resources to growth.

| Value | Effect |
|-------|--------|
| 0.0 | Wasteful metabolism |
| 0.5 | Normal efficiency |
| 1.0 | Highly efficient resource usage |

**Effect:** Higher efficiency allows survival in nutrient-poor environments.

---

### Body Color (RGB)

The color used to render **interior cells** of the colony.

```c
typedef struct {
    uint8_t r;  // Red (0-255)
    uint8_t g;  // Green (0-255)
    uint8_t b;  // Blue (0-255)
} Color;
```

Generated with vibrant, saturated values (components 50-255).

---

### Border Color (RGB)

The color used to render **border cells** (cells adjacent to empty or enemy cells).

Typically a darker variant of the body color:
```c
border_color.r = body_color.r / 2;
border_color.g = body_color.g / 2;
border_color.b = body_color.b / 2;
```

## Mutation Mechanics

### When Mutations Occur

Mutations are applied **once per colony per tick** in the `simulation_mutate()` phase:

```c
void simulation_mutate(World* world) {
    for (size_t i = 0; i < world->colony_count; i++) {
        if (world->colonies[i].active) {
            genome_mutate(&world->colonies[i].genome);
        }
    }
}
```

### Mutation Algorithm

Each mutable field has an independent chance to mutate:

```c
#define MUTATION_DELTA 0.1f

void genome_mutate(Genome* genome) {
    float mutation_chance = genome->mutation_rate;
    
    // Each field has mutation_chance probability of mutating
    if (rand_float() < mutation_chance) {
        // Delta: random value in [-0.1, +0.1]
        float delta = (rand_float() - 0.5f) * 2.0f * MUTATION_DELTA;
        genome->spread_rate = clamp(genome->spread_rate + delta, 0.0, 1.0);
    }
    
    if (rand_float() < mutation_chance) {
        float delta = (rand_float() - 0.5f) * 2.0f * MUTATION_DELTA;
        genome->mutation_rate = clamp(genome->mutation_rate + delta, 0.0, 1.0);
    }
    
    // ... repeat for aggression, resilience, metabolism
    
    // Social traits mutate with appropriate ranges
    if (rand_float() < mutation_chance) {
        float delta = (rand_float() - 0.5f) * 2.0f * MUTATION_DELTA * 0.4f;
        genome->detection_range = clamp(genome->detection_range + delta, 0.1, 0.5);
    }
    
    if (rand_float() < mutation_chance) {
        float delta = (rand_float() - 0.5f) * 2.0f * MUTATION_DELTA * 2.0f;
        genome->social_factor = clamp(genome->social_factor + delta, -1.0, 1.0);
    }
    
    if (rand_float() < mutation_chance) {
        float delta = (rand_float() - 0.5f) * 2.0f * MUTATION_DELTA * 0.3f;
        genome->merge_affinity = clamp(genome->merge_affinity + delta, 0.0, 0.3);
    }
}
```

### Mutation Characteristics

| Property | Behavior |
|----------|----------|
| Delta range | ±10% of full scale |
| Distribution | Uniform random |
| Clamping | Values stay in valid ranges |
| Self-modification | mutation_rate can mutate itself |

## Genetic Distance

Genetic distance quantifies how **similar** two genomes are.

### Calculation

```c
float genome_distance(const Genome* a, const Genome* b) {
    float diff = 0.0f;
    diff += abs(a->spread_rate - b->spread_rate);
    diff += abs(a->mutation_rate - b->mutation_rate);
    diff += abs(a->aggression - b->aggression);
    diff += abs(a->resilience - b->resilience);
    diff += abs(a->metabolism - b->metabolism);
    
    // Social traits (scaled to 0-1 range)
    diff += abs(a->detection_range - b->detection_range) * 2.5f;  // 0-0.4 → 0-1
    diff += abs(a->social_factor - b->social_factor) * 0.5f;      // -1 to 1 → 0-1
    diff += abs(a->merge_affinity - b->merge_affinity) * 3.33f;   // 0-0.3 → 0-1
    
    // Normalize to 0-1 range (8 fields, max diff each is ~1.0)
    return diff / 8.0f;
}
```

### Distance Interpretation

| Distance | Relationship |
|----------|--------------|
| 0.0 | Identical genomes |
| 0.0 - 0.1 | Very similar (likely recent relatives) |
| 0.1 - 0.2 | Similar (compatible for recombination) |
| 0.2 - 0.5 | Different species |
| 0.5 - 1.0 | Very different |

### Compatibility Check

```c
bool genome_compatible(const Genome* a, const Genome* b, float threshold) {
    return genome_distance(a, b) <= threshold;
}
```

**Default threshold:** 0.2 (20% maximum difference for recombination)

## Colony Division

When a colony becomes **geographically disconnected**, it divides into separate species.

### Detection Algorithm

1. Run flood-fill from each cell of the colony
2. Count connected components
3. If more than one component exists, the colony has split

```c
int* find_connected_components(World* world, uint32_t colony_id, 
                               int* num_components);
```

### Division Process

```
 Before Division:            After Division:
 ┌───────────────────┐       ┌───────────────────┐
 │ ●●●●●       ●●●●  │       │ AAAA        BBBB  │
 │ ●●●●●       ●●●●  │   →   │ AAAA        BBBB  │
 │ ●●●●●       ●●●●  │       │ AAAA        BBBB  │
 └───────────────────┘       └───────────────────┘
 Colony 1 (1 species)        Colony 1A + Colony 1B
```

### Inheritance Rules

1. **Largest component** keeps the original colony identity
2. **Smaller components** become new colonies with:
   - New unique ID
   - New scientific name
   - Genome copied from parent
   - Immediate mutation applied
   - Parent ID set to original colony

```c
Colony new_colony;
new_colony.genome = parent->genome;
genome_mutate(&new_colony.genome);  // Immediate divergence
new_colony.parent_id = parent->id;
```

## Recombination

When genetically compatible colonies touch, they can **merge** into a single colony.

### Triggering Conditions

1. Two colonies have adjacent cells
2. Colonies are genetically compatible (distance ≤ 0.2)
3. Only one recombination per tick (stability)

### Merge Process

```
 Before Merge:               After Merge:
 ┌───────────────────┐       ┌───────────────────┐
 │ AAAAA   BBBBB     │       │ CCCCCCCCCCC       │
 │ AAAAABBBBBB       │   →   │ CCCCCCCCCCC       │
 │ AAAAA   BBBBB     │       │ CCCCCCCCCCC       │
 └───────────────────┘       └───────────────────┘
 Colony A + Colony B         Colony C (merged)
```

### Genome Merging

Genomes are merged using **weighted averaging** based on cell counts:

```c
Genome genome_merge(const Genome* a, size_t count_a, 
                    const Genome* b, size_t count_b) {
    float total = count_a + count_b;
    float weight_a = count_a / total;
    float weight_b = count_b / total;
    
    Genome result;
    result.spread_rate = a->spread_rate * weight_a + 
                         b->spread_rate * weight_b;
    // ... repeat for all float fields
    
    // Social traits are also blended
    result.detection_range = a->detection_range * weight_a + 
                             b->detection_range * weight_b;
    result.social_factor = a->social_factor * weight_a + 
                           b->social_factor * weight_b;
    result.merge_affinity = a->merge_affinity * weight_a + 
                            b->merge_affinity * weight_b;
    result.max_tracked = (uint8_t)((a->max_tracked * weight_a + 
                                    b->max_tracked * weight_b) + 0.5f);
    
    // Colors are also blended
    result.body_color.r = a->body_color.r * weight_a + 
                          b->body_color.r * weight_b;
    // ...
    
    return result;
}
```

### Merge Winner

The **larger colony** absorbs the smaller one:
- Larger colony keeps its ID and name
- Smaller colony is marked inactive
- All cells transfer to larger colony

## Evolution Over Time

### Natural Selection Pressures

| Trait | Selection Pressure |
|-------|-------------------|
| High spread_rate | Faster expansion, but may overextend |
| High aggression | Captures territory, but creates conflicts |
| High resilience | Survives attacks, but passive |
| High mutation_rate | Adapts quickly, but unstable |
| Balanced traits | Sustainable growth |

### Emergent Behaviors

Over many ticks, you may observe:

1. **Stabilization**: Mutation rates trend toward moderate values
2. **Arms races**: Aggression vs. resilience escalation
3. **Speciation**: Isolated populations diverge
4. **Dominance**: Well-adapted genomes spread

### Lineage Tracking

Each colony tracks its parent:
```c
colony.parent_id  // 0 if original, else parent colony ID
```

This enables future features like family trees and ancestry analysis.

## Frontier Genetics Telemetry

Ferox exposes per-tick frontier genetics telemetry for seeded runs. The reference implementation is in `src/server/frontier_metrics.c` and is consumed in `tests/test_performance_eval.c`.

### Output Format

Telemetry is emitted in logfmt key-value format so it remains machine-readable while matching existing perf logging style:

```text
[perf] frontier_telemetry seed=48048 tick=30 frontier_sector_count=12 lineage_diversity_proxy=0.643112 lineage_entropy_bits=2.418337 frontier_cells=1984 occupied_cells=10954 active_lineages=7
```

### Definitions and Units

| Metric | Definition | Units | Range |
|--------|------------|-------|-------|
| `frontier_sector_count` | Number of occupied angular sectors (16 total) that contain at least one frontier cell around world occupancy centroid | sectors | 0-16 |
| `lineage_diversity_proxy` | Frontier heterozygosity proxy: `1 - Σ(p_i^2)` over root-lineage shares on frontier cells | unitless | 0-1 |
| `lineage_entropy_bits` | Shannon entropy across root-lineage shares over all occupied cells: `-Σ(p_i log2 p_i)` | bits | >=0 |

Additional metadata fields in each sample:
- `seed`: RNG seed used to initialize run.
- `tick`: simulation tick for the sample.
- `frontier_cells`, `occupied_cells`, `active_lineages`: supporting counts for analysis.

## Colony Visualization System

Colonies in Ferox are rendered using **cell-based rendering**, where each cell in the simulation grid is drawn directly. This provides accurate territory visualization.

### Cell-Based Rendering (Current)

The client receives the actual grid data and renders each cell:

```c
typedef struct Genome {
    // ... other fields ...
    Color body_color;    // Interior cell color
    Color border_color;  // Border cell color (adjacent to empty/enemy)
} Genome;
```

**Rendering process:**
1. Server sends RLE-compressed grid with cell `colony_id` values
2. Client decompresses grid to `uint32_t` array
3. For each cell, look up colony color and draw as colored square
4. Border cells (adjacent to different/empty cells) use `border_color`

```c
// Determine if cell is a border cell
bool is_border_cell(const uint32_t* grid, int w, int h, int x, int y) {
    uint32_t my_id = grid[y * w + x];
    if (my_id == 0) return false;
    
    // Check 4-connected neighbors
    int dx[] = {0, 1, 0, -1};
    int dy[] = {-1, 0, 1, 0};
    for (int i = 0; i < 4; i++) {
        int nx = x + dx[i], ny = y + dy[i];
        if (nx < 0 || nx >= w || ny < 0 || ny >= h) return true;
        if (grid[ny * w + nx] != my_id) return true;
    }
    return false;
}
```

### Benefits of Cell-Based Rendering

| Benefit | Description |
|---------|-------------|
| **Accurate** | Shows actual cell ownership from simulation |
| **Consistent** | All clients render identically |
| **Simple** | No complex shape computation needed |
| **GPU-Friendly** | Grid maps directly to texture/buffer |

### Deprecated: Procedural Shape System

> **Note:** The following system has been deprecated in favor of cell-based rendering. This documentation is preserved for historical reference.

The previous system used `shape_seed` and `wobble_phase` fields to procedurally generate blob shapes:

```c
// DEPRECATED - no longer used
uint32_t shape_seed;   // Was: Seed for procedural shape generation
float wobble_phase;    // Was: Animation phase for border movement

// DEPRECATED function
float colony_shape_at_angle(uint32_t shape_seed, float angle, float phase);
```

This approach generated approximate organic shapes using fractal noise, but did not accurately represent actual cell positions. The new cell-based rendering provides:
- True territory boundaries
- No approximation errors
- Direct correspondence with simulation state

## API Reference

### Creation

```c
// Create a random genome with valid ranges
// Sets minimum viable values for spread_rate and metabolism
// Initializes all social traits to random values within their ranges
Genome genome_create_random(void);
```

### Mutation

```c
// Apply mutations based on mutation_rate
// Mutates all traits including social behavior traits
void genome_mutate(Genome* genome);
```

### Comparison

```c
// Calculate genetic distance (0-1 scale)
// Includes social traits in distance calculation
float genome_distance(const Genome* a, const Genome* b);

// Check compatibility for recombination
// Note: merge_affinity can increase effective threshold
bool genome_compatible(const Genome* a, const Genome* b, float threshold);
```

### Merging

```c
// Merge two genomes weighted by cell counts
// All traits including social traits are blended
Genome genome_merge(const Genome* a, size_t count_a, 
                    const Genome* b, size_t count_b);
```

## Configuration Recommendations

### Aggressive Simulation

```c
Genome g;
g.spread_rate = 0.8;
g.mutation_rate = 0.08;
g.aggression = 0.7;
g.resilience = 0.3;
```

### Stable Ecosystem

```c
Genome g;
g.spread_rate = 0.4;
g.mutation_rate = 0.03;
g.aggression = 0.4;
g.resilience = 0.6;
```

### High Evolution

```c
Genome g;
g.mutation_rate = 0.10;  // Maximum mutation
g.spread_rate = 0.5;     // Moderate spread
```

### Social Clustering

```c
Genome g;
g.social_factor = 0.8;   // Strong attraction to neighbors
g.merge_affinity = 0.2;  // Higher merge compatibility
g.aggression = 0.2;      // Low aggression
g.resilience = 0.7;      // Good defense
```

### Territorial Isolation

```c
Genome g;
g.social_factor = -0.8;  // Repelled by neighbors
g.merge_affinity = 0.0;  // Minimal merge compatibility
g.aggression = 0.8;      // High aggression
g.detection_range = 0.4; // Long-range detection
```
