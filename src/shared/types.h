#ifndef FEROX_TYPES_H
#define FEROX_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Direction indices for spread_weights
typedef enum {
    DIR_N = 0,
    DIR_NE,
    DIR_E,
    DIR_SE,
    DIR_S,
    DIR_SW,
    DIR_W,
    DIR_NW,
    DIR_COUNT
} Direction;

#define WOBBLE_POINTS 8  // Legacy, kept for compatibility
#define SHAPE_SEED_OCTAVES 4  // Number of noise octaves for shape generation

// Color structure - RGB values (0-255)
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} Color;

// Genome structure - the genetic code of a colony
typedef struct {
    // === Basic Traits ===
    float spread_weights[8];  // 0-1 for each direction: N,NE,E,SE,S,SW,W,NW
    float spread_rate;        // 0-1: overall probability of spreading per tick
    float mutation_rate;      // 0-0.1
    float aggression;         // 0-1
    float resilience;         // 0-1
    float metabolism;         // 0-1: affects growth speed
    
    // === Social Behavior (chemotaxis-like) ===
    float detection_range;    // 0-1: how far can detect neighbors (scaled to world size)
    uint8_t max_tracked;      // 1-4: how many neighbor colonies can be tracked
    float social_factor;      // -1 to 1: negative=repelled, positive=attracted
    float merge_affinity;     // 0-1: slight bonus to merging with genetically similar colonies
    
    // === Environmental Sensing ===
    float nutrient_sensitivity;  // 0-1: how strongly to follow nutrient gradients
    float edge_affinity;         // -1 to 1: negative=avoid edges, positive=seek edges
    float density_tolerance;     // 0-1: how well colony handles crowding (affects spread in dense areas)
    
    // === Colony-Colony Interactions ===
    float toxin_production;      // 0-1: how much toxin is emitted (damages nearby foreign cells)
    float toxin_resistance;      // 0-1: resistance to toxin damage
    float signal_emission;       // 0-1: strength of chemical signals emitted
    float signal_sensitivity;    // 0-1: how strongly to react to signals
    float gene_transfer_rate;    // 0-0.1: probability of horizontal gene transfer on contact
    
    // === Survival Strategies ===
    float dormancy_threshold;    // 0-1: population ratio that triggers dormancy (0=never dormant)
    float dormancy_resistance;   // 0-1: how resistant dormant cells are (but can't grow)
    float biofilm_investment;    // 0-1: trade growth for resilience
    float motility;              // 0-1: how much the colony can drift/move
    float motility_direction;    // 0-2π: preferred drift direction (can evolve)
    
    // === Metabolic Strategy ===
    float efficiency;            // 0-1: high=slow but sustainable, low=fast but depletes resources
    
    Color body_color;
    Color border_color;
} Genome;

// Cell structure - represents one grid cell
typedef struct {
    uint32_t colony_id;  // 0 = empty
    bool is_border;
    uint8_t age;         // ticks since colonized
    int8_t component_id; // used during flood-fill, -1 = unmarked
} Cell;

// Colony state flags
typedef enum {
    COLONY_STATE_NORMAL = 0,
    COLONY_STATE_DORMANT = 1,    // Colony is dormant (resistant but not growing)
    COLONY_STATE_STRESSED = 2,   // Colony is under stress (low nutrients/toxins)
} ColonyState;

// Colony structure
typedef struct {
    uint32_t id;
    char name[64];       // Scientific name
    Genome genome;
    size_t cell_count;
    size_t max_cell_count;   // Historical max population
    uint64_t age;        // Ticks alive
    uint32_t parent_id;  // 0 if original
    bool active;         // Whether colony is alive
    Color color;         // Display color
    uint32_t shape_seed; // Seed for procedural shape generation
    float wobble_phase;  // Animation phase for border movement
    
    // New dynamic state
    ColonyState state;       // Current colony state
    float stress_level;      // 0-1: accumulated stress
    float drift_x, drift_y;  // Accumulated motility drift
    float signal_strength;   // Current signal output level
} Colony;

// World structure
typedef struct {
    int width;
    int height;
    Cell* cells;            // flat array: cells[y * width + x]
    Colony* colonies;       // dynamic array of colonies
    size_t colony_count;
    size_t colony_capacity;
    uint64_t tick;
    
    // Environmental layers
    float* nutrients;       // nutrient level per cell (0-1)
    float* toxins;          // toxin level per cell (0-1) 
    float* signals;         // chemical signal level per cell (0-1)
    uint32_t* signal_source; // which colony emitted signal at each cell
} World;

#endif // FEROX_TYPES_H
