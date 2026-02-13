# Ferox Simulation Mechanics

This document explains how the simulation works, including the world grid, tick cycle, and all simulation phases.

## Current Behavior Snapshot

- **Atomic path order (`atomic_tick`)**: parallel age → parallel spread (CAS) → sync to `World` → nutrient update → scent update → combat resolution → cell turnover/death → mutation → division check → recombination check → dynamic colony spawn → stats/behavior update → sync back.
- **Spread dynamics**: 8-neighbor spreading from occupied cells only, with age-0 cascade prevention; spread claims empty cells only in atomic phase (`neighbor_colony != 0` is skipped, not overtaken there).
- **Strategy archetypes**: new genomes are seeded from 8 archetypes in `genome_create_random()` (`BERSERKER`, `TURTLE`, `SWARM`, `TOXIC`, `HIVE`, `NOMAD`, `PARASITE`, `CHAOTIC`), then mutated over time.
- **Scent/quorum/biofilm/dormancy**: scent and neighbor sampling bias spread direction; quorum activation is derived from `signal_strength` vs `quorum_threshold`; biofilm grows/decays each tick; dormancy is stress-triggered and reduces turnover death while suppressing expansion pressure.

## World Grid Structure

The simulation takes place on a 2D rectangular grid.

### Grid Layout

```
     0   1   2   3   4   5   6   7   8   9  ... width-1
   ┌───┬───┬───┬───┬───┬───┬───┬───┬───┬───┬───┐
 0 │   │   │ A │ A │ A │   │   │ B │ B │   │   │
   ├───┼───┼───┼───┼───┼───┼───┼───┼───┼───┼───┤
 1 │   │ A │ A │ A │ A │ A │   │ B │ B │ B │   │
   ├───┼───┼───┼───┼───┼───┼───┼───┼───┼───┼───┤
 2 │   │ A │ A │ A │ A │ A │   │ B │ B │ B │   │
   ├───┼───┼───┼───┼───┼───┼───┼───┼───┼───┼───┤
 3 │   │   │ A │ A │ A │   │   │   │ B │   │   │
   ├───┼───┼───┼───┼───┼───┼───┼───┼───┼───┼───┤
 . │   │   │   │   │   │   │   │   │   │   │   │
 . │   │   │   │   │   │   │   │   │   │   │   │
   └───┴───┴───┴───┴───┴───┴───┴───┴───┴───┴───┘
                                          height-1
```

### Cell Storage

Cells are stored in a flat array with row-major ordering:

```c
// Access cell at (x, y)
Cell* cell = &world->cells[y * world->width + x];
```

### Cell Structure

```c
typedef struct {
    uint32_t colony_id;   // 0 = empty, otherwise colony ID
    bool is_border;       // Adjacent to different/empty cell
    uint8_t age;          // Ticks since colonization (0-255)
    int8_t component_id;  // Used during flood-fill (-1 = unmarked)
} Cell;
```

### Cell States

| colony_id | State |
|-----------|-------|
| 0 | Empty (unoccupied) |
| 1+ | Occupied by colony with that ID |

## Simulation Tick Cycle

Each simulation tick executes these phases in order:

```
┌──────────────────────────────────────────────────────────────────┐
│                     ATOMIC TICK CYCLE                             │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│   ═══════════════════ PARALLEL PHASE ════════════════════════   │
│                                                                  │
│   ┌─────────────┐                                                │
│   │ 1. Age Cells│  Atomic age++ for all occupied cells           │
│   └──────┬──────┘  (parallel across regions)                     │
│          │                                                       │
│          ▼                                                       │
│   ┌──────────────┐                                               │
│   │ 2. Spread    │  Atomic CAS for cell claims                   │
│   └──────┬───────┘  (parallel, lock-free)                        │
│          │                                                       │
│          ▼                                                       │
│   ┌──────────────┐                                               │
│   │ Barrier Sync │  Wait for all threads                         │
│   └──────┬───────┘                                               │
│          │                                                       │
│   ═══════════════════ SERIAL PHASE ══════════════════════════   │
│          │                                                       │
│          ▼                                                       │
│   ┌──────────────┐                                               │
│   │ 3. Mutate    │  Apply mutations to colony genomes            │
│   └──────┬───────┘                                               │
│          │                                                       │
│          ▼                                                       │
│   ┌─────────────────┐                                            │
│   │ 4. Check        │  Detect and handle colony splits           │
│   │    Divisions    │                                            │
│   └──────┬──────────┘                                            │
│          │                                                       │
│          ▼                                                       │
│   ┌─────────────────────┐                                        │
│   │ 5. Check            │  Merge compatible adjacent colonies    │
│   │    Recombinations   │                                        │
│   └──────┬──────────────┘                                        │
│          │                                                       │
│          ▼                                                       │
│   ┌──────────────────┐                                           │
│   │ 6. Update Colony │  Track max pop, check death, animate     │
│   │    Stats         │  wobble phase                            │
│   └──────┬───────────┘                                           │
│          │                                                       │
│          ▼                                                       │
│   ┌──────────────────┐                                           │
│   │ 7. Increment     │  world->tick++                            │
│   │    World Tick    │                                           │
│   └──────────────────┘                                           │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
```

### Implementation

```c
void atomic_tick(AtomicWorld* aworld) {
    // === PARALLEL PHASE ===
    
    // Age all cells in parallel
    atomic_age(aworld);
    atomic_barrier(aworld);
    
    // Spread colonies in parallel using atomic CAS
    atomic_spread(aworld);
    atomic_barrier(aworld);
    
    // === SERIAL PHASE ===
    // Sync atomic state back to regular world
    atomic_world_sync_to_world(aworld);
    
    simulation_update_nutrients(world);
    simulation_update_scents(world);
    simulation_resolve_combat(world);
    atomic_apply_cell_turnover(world);

    simulation_mutate(world);
    simulation_check_divisions(world);
    simulation_check_recombinations(world);
    atomic_spawn_dynamic_colonies(world);
    simulation_update_colony_stats(world);
    atomic_update_colony_behavior(world);
    
    // Sync changes back to atomic world
    atomic_world_sync_from_world(aworld);
    
    world->tick++;
}
```

## Phase 1: Age Cells (Atomic)

Parallel aging using atomic operations:

```c
void atomic_age_region(AtomicRegionWork* work) {
    DoubleBufferedGrid* grid = &work->aworld->grid;
    
    for (int y = work->start_y; y < work->end_y; y++) {
        for (int x = work->start_x; x < work->end_x; x++) {
            AtomicCell* cell = grid_get_cell(grid, x, y);
            if (cell && atomic_load(&cell->colony_id) != 0) {
                atomic_cell_age(cell);  // Atomic increment, saturates at 255
            }
        }
    }
}
```

- Age is capped at 255 (uint8_t max)
- Empty cells have undefined age
- Age resets to 0 when a cell is colonized

## Phase 2: Spreading (Atomic)

Colonies expand by colonizing adjacent cells using lock-free atomic operations.

### Cascade Prevention

Cells with age=0 (newly claimed in the current tick) do not spread. This prevents exponential "cascade" growth where newly claimed cells immediately spread to their neighbors within the same tick:

```c
// Skip cells that were just claimed this tick
uint8_t age = atomic_load(&cell->age);
if (age == 0) continue;  // Wait until next tick to spread
```

This ensures smooth, realistic colony growth (e.g., 1→4→14→31) instead of explosive expansion (e.g., 1→211→2400+).

### Atomic Spreading Algorithm

```
For each occupied cell (x, y) in parallel:
    If cell.age == 0: continue (newly claimed, skip this tick)
    
    For each neighbor in 8-connectivity (N,NE,E,SE,S,SW,W,NW):
        Calculate social_influence = calculate_social_influence(...)
        If neighbor is empty:
            If random() < spread_rate × metabolism × direction_weight × social_influence:
                CAS: try to claim cell (only succeeds if still empty)
                If success: increment population atomically
        Else if neighbor belongs to enemy:
            Skip in atomic spread phase (combat is handled later in serial phase)

// No pending queue needed - CAS resolves conflicts instantly
```

### Social Influence on Spreading

Before attempting to spread, colonies calculate a social influence multiplier based on nearby detected neighbors:

```c
float social_mult = calculate_social_influence(world, grid, x, y, dx, dy, colony);
spread_prob *= social_mult;  // Multiplier in range [0.3, 2.0]
```

**Influence Calculation:**

1. **Neighbor Detection**: Colony scans cells within `detection_range` (% of world size)
2. **Direction Calculation**: Compute vector toward/away from nearest detected neighbor
3. **Alignment**: Calculate how well spread direction aligns with social vector
4. **Multiplier**: `1.0 + social_factor × alignment × 0.5`

**Effect on Spread Probability:**

| Social Factor | Spreading Toward Neighbor | Spreading Away from Neighbor |
|---------------|---------------------------|------------------------------|
| +1.0 (attracted) | 1.5x probability | 0.5x probability |
| 0.0 (neutral) | 1.0x probability | 1.0x probability |
| -1.0 (repelled) | 0.5x probability | 1.5x probability |

This creates emergent chemotaxis-like behavior:
- Colonies with positive `social_factor` cluster toward neighbors
- Colonies with negative `social_factor` spread into empty territories

### 8-Connectivity (Moore Neighborhood)

The atomic simulation uses 8-connected neighbors for more natural spreading:

```
    NW       N       NE
    (-1,-1)  (0,-1)  (+1,-1)
       ↖      ↑      ↗
         ╲    │    ╱
   W ← ─ ─ ─ ● ─ ─ ─ → E
   (-1,0)    │      (+1,0)
         ╱   │    ╲
       ↙     ↓      ↘
    SW       S       SE
    (-1,+1) (0,+1)  (+1,+1)
```

Direction offsets:
```c
static const int DX8[] = {0, 1, 1, 1, 0, -1, -1, -1};
static const int DY8[] = {-1, -1, 0, 1, 1, 1, 0, -1};
```

### Atomic Compare-and-Swap (CAS)

The key insight is using CAS for cell ownership instead of locks or pending buffers:

```c
// Thread 1 tries to claim empty cell for colony A
// Thread 2 simultaneously tries to claim same cell for colony B

// Thread 1: CAS(cell, expected=0, desired=A)
// Thread 2: CAS(cell, expected=0, desired=B)

// Exactly ONE succeeds - no race conditions, no locks
// The "loser" simply fails and moves on
```

This enables true parallel spreading without synchronization overhead:

```c
bool atomic_cell_try_claim(AtomicCell* cell, uint32_t expected, uint32_t desired) {
    return atomic_compare_exchange_strong(&cell->colony_id, &expected, desired);
}

// In spread loop:
if (atomic_cell_try_claim(neighbor, 0, colony_id)) {
    atomic_store(&neighbor->age, 0);
    atomic_stats_add_cell(&colony_stats[colony_id]);
}
```

### Thread-Local RNG

Each thread uses its own xorshift32 RNG state to avoid contention:

```c
static inline uint32_t xorshift32(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}
```

This provides:
- No locks on random number generation
- Deterministic results (given same initial seeds)
- Cache-friendly access pattern

### Atomic Population Tracking

Population counts are updated atomically during spreading:

```c
typedef struct {
    _Atomic int64_t cell_count;      // Current population
    _Atomic int64_t max_cell_count;  // Historical max
    _Atomic uint64_t generation;     // Mutation counter
} AtomicColonyStats;

// Lock-free population increment with max tracking
static inline void atomic_stats_add_cell(AtomicColonyStats* stats) {
    int64_t new_count = atomic_fetch_add(&stats->cell_count, 1) + 1;
    // Update max using CAS loop
    int64_t old_max = atomic_load(&stats->max_cell_count);
    while (new_count > old_max) {
        if (atomic_compare_exchange_weak(&stats->max_cell_count, &old_max, new_count)) {
            break;
        }
    }
}
```

### Combat Mechanics

Combat occurs when colonies meet at borders. The system is strategic with multiple factors:

#### Combat Resolution Flow

```
┌────────────────────────────────────────────────────────────────┐
│                    COMBAT RESOLUTION                            │
├────────────────────────────────────────────────────────────────┤
│                                                                │
│  1. Toxin Emission Phase                                       │
│     ├─ Border cells emit toxins (toxin_production trait)      │
│     └─ Toxins spread to neighboring cells                     │
│                                                                │
│  2. Combat Calculation Phase (for each border cell)           │
│     ├─ Base strength: aggression vs resilience                │
│     ├─ Flanking bonus: friendly neighbors boost attack        │
│     ├─ Defensive formation: defense_priority + neighbors      │
│     ├─ Directional preference: spread_weights affect push     │
│     ├─ Toxin warfare: production/resistance modifiers         │
│     ├─ Biofilm defense: absorbs incoming damage               │
│     ├─ Nutrient advantage: well-fed cells fight harder        │
│     ├─ Toxin damage: cells in toxins fight worse              │
│     ├─ Momentum: success_history boosts confidence            │
│     ├─ Stress effects: desperate attacks vs crumbling defense │
│     └─ Size advantage: larger colonies get morale bonus       │
│                                                                │
│  3. Resolution Phase                                           │
│     ├─ Calculate win probability from attack/defend strength  │
│     ├─ Winner captures cell, loser loses it                   │
│     ├─ Update stress levels (winners decrease, losers inc)    │
│     └─ Update success_history for learning                    │
│                                                                │
└────────────────────────────────────────────────────────────────┘
```

#### Combat Strength Calculation

```c
// Attacker strength
attack_str = aggression * 1.2f
           * flanking_bonus           // 1.0 + (friendly_neighbors * 0.15)
           * direction_weight         // 0.7-1.3 based on spread_weights
           + toxin_production * 0.4f
           * nutrient_modifier        // 0.6-1.1 based on local nutrients
           * (1 - toxin_damage)       // Reduced by enemy toxins
           * momentum_bonus           // 0.8-1.2 from success_history

// Defender strength  
defend_str = resilience * 1.0f
           * defensive_bonus          // 1.0 + (defense_priority * friendly * 0.2)
           + toxin_resistance * 0.3f
           * biofilm_strength         // 1.0-1.3 from biofilm_strength
           * nutrient_modifier
           * (1 - toxin_damage)

// Win probability
attack_chance = attack_str / (attack_str + defend_str + 0.1f)
success = random() < attack_chance * 0.7f
```

#### Strategic Factors

| Factor | Trait | Effect |
|--------|-------|--------|
| Flanking | - | +15% attack per friendly neighbor |
| Defensive wall | defense_priority | +20% defense per friendly neighbor |
| Directional push | spread_weights | 70-130% attack based on direction |
| Toxin offense | toxin_production | +40% attack, creates hostile zone |
| Toxin defense | toxin_resistance | +30% defense, resists toxin damage |
| Biofilm armor | biofilm_strength | +30% defense at borders |
| Well-fed | nutrients | +50% attack/defense in nutrient-rich areas |
| Size advantage | cell_count | ±15% based on relative colony size |
| Momentum | success_history | ±20% based on past success |

#### Learning System

Colonies learn from combat outcomes:

```c
// On successful attack
success_history[direction] += 0.05 * learning_rate

// On failed attack
success_history[direction] -= 0.02 * learning_rate

// Decay over time (memory fades)
success_history[d] *= (0.995 + memory_factor * 0.004)
```

#### Stress and State

Colonies track stress levels that affect behavior:

| State | Trigger | Effect |
|-------|---------|--------|
| Normal | stress < 0.5 | Standard combat behavior |
| Stressed | stress 0.5-0.7 | Non-defensive colonies crumble faster |
| Dormant | stress > sporulation_threshold | Stop attacking, become resistant |

Defensive colonies under high stress stop attacking (tactical retreat).

## Phase 3: Mutation

Apply genetic mutations to all active colonies.

```c
void simulation_mutate(World* world) {
    for (size_t i = 0; i < world->colony_count; i++) {
        if (world->colonies[i].active) {
            genome_mutate(&world->colonies[i].genome);
        }
    }
}
```

See [GENETICS.md](GENETICS.md) for mutation details.

## Phase 4: Division Detection

Detect when colonies have become geographically disconnected.

### Problem

A colony can become split when:
- An enemy colony cuts through it
- Cells die or are captured
- Growth creates isolated pockets

```
 Before:                  After enemy attack:
 ┌──────────────────┐     ┌──────────────────┐
 │ A A A A A A A A  │     │ A A A │ │ A A A A│
 │ A A A A A A A A  │     │ A A A │B│ A A A A│
 │ A A A A A A A A  │     │ A A A │B│ A A A A│
 └──────────────────┘     └──────────────────┘
   Connected                Disconnected!
```

### Flood-Fill Algorithm

We use iterative flood-fill to find connected components:

```c
int* find_connected_components(World* world, uint32_t colony_id, 
                               int* num_components) {
    // 1. Reset component markers
    for each cell belonging to colony_id:
        cell->component_id = -1
    
    // 2. Find components via flood-fill
    int component_count = 0;
    for each cell (x, y) belonging to colony_id:
        if cell->component_id == -1:
            // Start new component
            sizes[component_count] = flood_fill(world, x, y, 
                                                colony_id, component_count)
            component_count++
    
    return sizes;  // Array of component sizes
}
```

### Stack-Based Flood-Fill

```c
static int flood_fill(World* world, int start_x, int start_y, 
                      uint32_t colony_id, int8_t comp_id) {
    Stack* stack = stack_create();
    int count = 0;
    
    stack_push(stack, start_x, start_y);
    start_cell->component_id = comp_id;
    
    while (!stack_empty(stack)) {
        int x, y;
        stack_pop(stack, &x, &y);
        count++;
        
        // Check 4 neighbors
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d];
            int ny = y + DY[d];
            
            Cell* neighbor = world_get_cell(world, nx, ny);
            if (neighbor && 
                neighbor->colony_id == colony_id && 
                neighbor->component_id == -1) {
                neighbor->component_id = comp_id;
                stack_push(stack, nx, ny);
            }
        }
    }
    
    return count;
}
```

### Division Handling

When a colony has multiple components:

1. **Keep largest component** with original colony
2. **Create new colonies** for smaller components:
   - New unique ID
   - New generated name
   - Copy parent genome
   - Apply immediate mutation
   - Record parent_id for lineage

```c
if (num_components > 1) {
    // Find largest component
    int largest_idx = find_max_index(sizes, num_components);
    
    // Create new colonies for other components
    for (int c = 0; c < num_components; c++) {
        if (c == largest_idx) continue;
        
        Colony new_colony;
        generate_scientific_name(new_colony.name, sizeof(new_colony.name));
        new_colony.genome = parent->genome;
        genome_mutate(&new_colony.genome);  // Immediate divergence
        new_colony.parent_id = parent->id;
        
        uint32_t new_id = world_add_colony(world, new_colony);
        
        // Update cells to new colony
        for each cell with component_id == c:
            cell->colony_id = new_id;
    }
    
    // Update original colony's cell count
    parent->cell_count = sizes[largest_idx];
}
```

## Phase 5: Recombination

Merge genetically compatible adjacent colonies that share a familial relationship.

### Relationship Requirement

Recombination only occurs between **related colonies**—colonies must have a parent-child or sibling relationship:

```c
// Check relationship - must be parent-child or siblings
if (colony_a->parent_id != colony_b->id && colony_b->parent_id != colony_a->id) {
    // Not parent-child, also check if siblings (same parent)
    if (colony_a->parent_id == 0 || colony_a->parent_id != colony_b->parent_id) {
        continue;  // Not related, no merge
    }
}
```

**Why this restriction?**
- Prevents all colonies from eventually merging into one super-colony
- Allows recently divided colonies to reconnect if they rejoin
- Creates stable ecosystem with multiple competing species
- Simulates biological species barriers

### Detection

Scan for adjacent cells belonging to different related colonies:

```c
void simulation_check_recombinations(World* world) {
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            Cell* cell = world_get_cell(world, x, y);
            if (!cell || cell->colony_id == 0) continue;
            
            Colony* colony_a = world_get_colony(world, cell->colony_id);
            
            // Check right and down neighbors only (avoid double-checking)
            for (int d = 1; d <= 2; d++) {  // E and S only
                int nx = x + DX[d];
                int ny = y + DY[d];
                
                Cell* neighbor = world_get_cell(world, nx, ny);
                if (!neighbor || neighbor->colony_id == 0 || 
                    neighbor->colony_id == cell->colony_id) continue;
                
                Colony* colony_b = world_get_colony(world, neighbor->colony_id);
                
                // Must be related (parent-child or siblings)
                if (!colonies_are_related(colony_a, colony_b)) continue;
                
                // Check genetic compatibility (stricter threshold: 0.05)
                if (genome_distance(&colony_a->genome, &colony_b->genome) <= 0.05f) {
                    perform_merge(colony_a, colony_b, world);
                    return;  // Only one merge per tick
                }
            }
        }
    }
}
```

### Merge Rules

1. **Must be related** (parent-child or sibling colonies)
2. **Genetic distance ≤ 0.05** (stricter than previous 0.2 threshold)
3. **Larger colony** absorbs smaller colony
4. **Genomes are blended** weighted by cell count
5. **Smaller colony** is deactivated
6. **Only one merge per tick** for stability

```c
// Determine winner
Colony* larger = colony_a->cell_count >= colony_b->cell_count 
                 ? colony_a : colony_b;
Colony* smaller = colony_a->cell_count >= colony_b->cell_count 
                  ? colony_b : colony_a;

// Merge genomes
larger->genome = genome_merge(&larger->genome, larger->cell_count,
                              &smaller->genome, smaller->cell_count);

// Transfer cells
for each cell belonging to smaller:
    cell->colony_id = larger->id;

larger->cell_count += smaller->cell_count;
smaller->cell_count = 0;
smaller->active = false;
```

### Compatibility Threshold

Default threshold is **0.2** (20% maximum genetic distance):

```c
bool genome_compatible(const Genome* a, const Genome* b, float threshold) {
    return genome_distance(a, b) <= threshold;
}
```

Lower thresholds create more species diversity; higher thresholds lead to more merging.

## Colony Lifecycle

```
┌───────────────────────────────────────────────────────────────────┐
│                        COLONY LIFECYCLE                           │
├───────────────────────────────────────────────────────────────────┤
│                                                                   │
│     ┌─────────────┐                                               │
│     │   SPAWN     │ ◄── Initial placement or manual spawn         │
│     │ (random or  │                                               │
│     │  command)   │                                               │
│     └──────┬──────┘                                               │
│            │                                                      │
│            │ world_add_colony()                                   │
│            │                                                      │
│            ▼                                                      │
│     ┌─────────────┐                                               │
│     │   GROWTH    │ ◄── simulation_spread()                       │
│     │  (active)   │                                               │
│     │             │     Tracks max_cell_count (peak population)   │
│     │ Spreading   │     Grid data sent to clients for rendering   │
│     │ Competing   │                                               │
│     │ Mutating    │                                               │
│     └──────┬──────┘                                               │
│            │                                                      │
│            ├─────────────────┬────────────────┐                   │
│            │                 │                │                   │
│            ▼                 ▼                ▼                   │
│     ┌─────────────┐   ┌─────────────┐  ┌─────────────┐            │
│     │  DIVISION   │   │ RECOMBINE   │  │    DEATH    │            │
│     │             │   │             │  │             │            │
│     │ Colony split│   │ Merge with  │  │ Population  │            │
│     │ into 2+ new │   │ compatible  │  │ reaches 0   │            │
│     │ species     │   │ neighbor    │  │             │            │
│     └──────┬──────┘   └──────┬──────┘  └──────┬──────┘            │
│            │                 │                │                   │
│            │ Children        │ Larger         │ active = false    │
│            │ continue        │ survives       │ cell_count = 0    │
│            ▼                 ▼                ▼                   │
│     ┌─────────────┐   ┌─────────────┐  ┌─────────────┐            │
│     │   GROWTH    │   │   GROWTH    │  │  INACTIVE   │            │
│     │  (children) │   │  (merged)   │  │  (dead)     │            │
│     └─────────────┘   └─────────────┘  └─────────────┘            │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘
```

### Colony Death

A colony dies when its population (cell_count) reaches zero:

```c
// In simulation_tick()
if (colony->cell_count == 0) {
    colony->active = false;
    continue;  // Skip further processing
}
```

**Causes of death:**
- All cells captured by aggressive neighboring colonies
- Gradual attrition from superior competitors
- Geographic isolation leading to elimination
- Size-based decay (natural cell death)

**Effects of death:**
- `active` flag set to `false`
- Colony hidden from info panel display
- Colony data retained for historical tracking

### Size-Based Decay

Larger colonies experience faster cell death due to resource transport limitations. This creates a natural equilibrium mechanism where mega-colonies face structural challenges:

```c
// Base death rate varies by position
float base_death_rate = 0.01f;  // Interior cells: 1%
if (cell->is_border) {
    base_death_rate = 0.035f;   // Border cells: 3.5% (more vulnerable)
}

// SIZE SCALING: Colonies over 50 cells decay faster
// Each additional 100 cells adds ~20% more decay
if (colony->cell_count > 50) {
    float size_factor = 1.0f + (colony->cell_count - 50) / 500.0f;
    base_death_rate *= size_factor;
}

// Interior starvation: Large colony interiors decay even faster
if (!cell->is_border && colony->cell_count > 100) {
    base_death_rate *= 1.3f;  // 30% faster interior death
}

// Protective factors
base_death_rate *= (1.0f - colony->biofilm_strength * 0.5f);  // Biofilm protection
base_death_rate *= (1.0f - colony->genome.efficiency * 0.4f); // Efficiency reduces decay
```

**Biological rationale:**
- Resources must travel from border to interior
- Larger colonies have longer resource transport paths
- Interior cells are "last to be fed" and starve first
- Creates boom/bust cycles and prevents monopolies

### Max Population Tracking

Each colony tracks its historical peak population:

```c
typedef struct Colony {
    // ...
    size_t cell_count;       // Current population
    size_t max_cell_count;   // Historical max population
    // ...
} Colony;
```

**Updated each tick:**
```c
if (colony->cell_count > colony->max_cell_count) {
    colony->max_cell_count = colony->cell_count;
}
```

This enables:
- Displaying "peak population" statistics
- Tracking colony prosperity over time
- Identifying dominant colonies

### Colony Stats Update

Each tick, active colonies update their statistics:

```c
// Update population tracking
if (colony->cell_count > colony->max_cell_count) {
    colony->max_cell_count = colony->cell_count;
}
```

The grid data (cell ownership) is sent to clients via RLE-compressed grid transmission, allowing accurate cell-based rendering of territories. See [PROTOCOL.md](PROTOCOL.md) for grid serialization details.

## Colony States

Colonies can exist in different behavioral states that affect their metabolism and survival strategies.

### State Transitions

```
┌─────────────────────────────────────────────────────────────────┐
│                     COLONY STATE MACHINE                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│     ┌─────────────┐     stress > dormancy_threshold             │
│     │   NORMAL    │────────────────────────────────►┌───────────┤
│     │             │                                 │ STRESSED  │
│     │ Full growth │◄────────────────────────────────│           │
│     │ Full spread │    stress decreases             └─────┬─────┤
│     └─────────────┘                                       │     │
│           ▲                                               │     │
│           │                                               ▼     │
│           │         ┌─────────────┐                             │
│           │         │  DORMANT    │◄───── continued stress      │
│           │         │             │                             │
│           │         │ Minimal     │                             │
│           └─────────│ metabolism  │                             │
│           recovery  │ High resist │                             │
│                     └─────────────┘                             │
└─────────────────────────────────────────────────────────────────┘
```

### State Behaviors

| State | Spread Rate | Resilience | Metabolism | Description |
|-------|-------------|------------|------------|-------------|
| NORMAL | Baseline from genome traits | Baseline | Baseline | Standard active growth |
| STRESSED | Context-dependent combat pressure | Context-dependent | Context-dependent | Transitional state when stress > 0.5 |
| DORMANT | Strongly reduced expansion pressure | Higher effective survival | Lower effective activity | Triggered by stress + sporulation/dormancy thresholds |

### Stress Accumulation

Stress increases from:
- **Toxin exposure** - toxins × (1 - toxin_resistance)
- **Nutrient scarcity** - (1 - local_nutrients) × nutrient_sensitivity
- **Overcrowding** - density × (1 - density_tolerance)
- **Combat losses** - lost cells from enemy attacks

Stress decreases when conditions improve.

## Environmental Layers

The world contains environmental layers that affect colony behavior.

### Nutrient Layer

```c
float* nutrients;  // Per-cell nutrient concentration (0-1)
```

**Dynamics:**
- Nutrients regenerate slowly over time
- Consumed by colony growth
- Colonies with high `nutrient_sensitivity` spread toward nutrient-rich areas

### Toxin Layer

```c
float* toxins;  // Per-cell toxin concentration (0-1)
```

**Dynamics:**
- Emitted by colonies with high `toxin_production`
- Diffuses to neighboring cells
- Decays over time
- Damages colonies based on (1 - toxin_resistance)

### Signal Layer

```c
float* signals;        // Chemical signal strength (0-1)
uint32_t* signal_source;  // Colony ID that emitted signal
```

**Dynamics:**
- Emitted by colonies based on `signal_emission`
- Diffuses rapidly
- Decays quickly
- Colonies respond based on `signal_sensitivity`

**Signal Uses:**
- Coordinate movement toward kin
- Mark territory
- Warn of dangers

## Border Detection

Cells are marked as border cells for rendering purposes:

```c
bool is_border(World* world, int x, int y) {
    Cell* cell = world_get_cell(world, x, y);
    if (!cell || cell->colony_id == 0) return false;
    
    // Check all 4 neighbors
    for (int d = 0; d < 4; d++) {
        Cell* neighbor = world_get_cell(world, x + DX[d], y + DY[d]);
        if (!neighbor || neighbor->colony_id != cell->colony_id) {
            return true;  // Adjacent to different/empty cell
        }
    }
    return false;
}
```

Border cells are rendered with `border_color`; interior cells with `body_color`.

## World Initialization

### Creating the World

```c
World* world_create(int width, int height) {
    World* world = malloc(sizeof(World));
    world->width = width;
    world->height = height;
    world->tick = 0;
    world->colony_count = 0;
    world->colony_capacity = 16;  // Initial capacity
    
    // Allocate cell grid
    world->cells = calloc(width * height, sizeof(Cell));
    
    // Initialize all cells as empty
    for (int i = 0; i < width * height; i++) {
        world->cells[i].colony_id = 0;
        world->cells[i].is_border = false;
        world->cells[i].age = 0;
        world->cells[i].component_id = -1;
    }
    
    // Allocate colony array
    world->colonies = malloc(colony_capacity * sizeof(Colony));
    
    return world;
}
```

### Spawning Initial Colonies

```c
void world_init_random_colonies(World* world, int count) {
    for (int i = 0; i < count; i++) {
        Colony colony;
        generate_scientific_name(colony.name, sizeof(colony.name));
        colony.genome = genome_create_random();
        colony.color = colony.genome.body_color;
        colony.cell_count = 0;
        colony.active = true;
        colony.age = 0;
        colony.parent_id = 0;
        
        uint32_t id = world_add_colony(world, colony);
        
        // Place at random empty position
        int x = rand_range(0, world->width - 1);
        int y = rand_range(0, world->height - 1);
        Cell* cell = world_get_cell(world, x, y);
        if (cell && cell->colony_id == 0) {
            cell->colony_id = id;
            colony.cell_count = 1;
        }
    }
}
```

## Performance Considerations

### Atomic Parallel Processing

The world is divided into regions for parallel processing with lock-free spreading:

```
┌───────────────┬───────────────┬───────────────┬───────────────┐
│   Region 0    │   Region 1    │   Region 2    │   Region 3    │
│  (Thread 1)   │  (Thread 2)   │  (Thread 3)   │  (Thread 4)   │
│               │               │               │               │
│ xorshift RNG  │ xorshift RNG  │ xorshift RNG  │ xorshift RNG  │
├───────────────┼───────────────┼───────────────┼───────────────┤
│   Region 4    │   Region 5    │   Region 6    │   Region 7    │
│  (Thread 1)   │  (Thread 2)   │  (Thread 3)   │  (Thread 4)   │
│               │               │               │               │
│ xorshift RNG  │ xorshift RNG  │ xorshift RNG  │ xorshift RNG  │
├───────────────┼───────────────┼───────────────┼───────────────┤
│   Region 8    │   Region 9    │   Region 10   │   Region 11   │
│  (Thread 1)   │  (Thread 2)   │  (Thread 3)   │  (Thread 4)   │
│               │               │               │               │
│ xorshift RNG  │ xorshift RNG  │ xorshift RNG  │ xorshift RNG  │
├───────────────┼───────────────┼───────────────┼───────────────┤
│   Region 12   │   Region 13   │   Region 14   │   Region 15   │
│  (Thread 1)   │  (Thread 2)   │  (Thread 3)   │  (Thread 4)   │
│               │               │               │               │
│ xorshift RNG  │ xorshift RNG  │ xorshift RNG  │ xorshift RNG  │
└───────────────┴───────────────┴───────────────┴───────────────┘
```

**No border synchronization needed:** Atomic CAS resolves conflicts at region boundaries automatically.

### Performance Improvements

| Metric | Lock-Based | Atomic-Based | Improvement |
|--------|------------|--------------|-------------|
| CPU Usage | 2-4% | 28%+ | ~10x better utilization |
| Synchronization | Mutexes + barriers | CAS only | No lock contention |
| Border conflicts | Requires buffering | CAS resolves instantly | Simpler code |
| GPU-ready | No | Yes | Direct CUDA/OpenCL mapping |

### Memory Access Patterns

- Row-major cell access for cache efficiency
- Flat contiguous arrays for GPU compatibility
- Double-buffered grid avoids read-write conflicts
- Thread-local RNG eliminates false sharing

### Tick Rate

Default tick rate is 100ms (10 ticks/second) when running the server directly. The `scripts/run.sh` helper uses 200ms by default for smoother visualization. Adjustable via:
- Server command-line option (`-r` or `--rate`)
- Client speed controls (MSG_COMMAND)
