#ifndef FEROX_TYPES_H
#define FEROX_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdatomic.h>

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

#define WOBBLE_POINTS 8
#define SHAPE_SEED_OCTAVES 4

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} Color;

typedef struct {
    float aggression;
    float resilience;
    float toxin_production;
    float toxin_resistance;
    float defense_priority;
} CombatStats;

typedef struct {
    float spread_rate;
    float mutation_rate;
    float metabolism;
    float efficiency;
    float resource_consumption;
} GrowthStats;

typedef struct {
    float detection_range;
    uint8_t max_tracked;
    float social_factor;
    float merge_affinity;
    float quorum_threshold;
    float signal_emission;
    float signal_sensitivity;
    float alarm_threshold;
    float gene_transfer_rate;
} BehaviorStats;

typedef struct {
    float spread_weights[8];
    float nutrient_sensitivity;
    float toxin_sensitivity;
    float edge_affinity;
    float density_tolerance;
    float dormancy_threshold;
    float dormancy_resistance;
    float sporulation_threshold;
    float biofilm_investment;
    float biofilm_tendency;
    float motility;
    float motility_direction;
    float specialization;
    float hidden_weights[8];
    float learning_rate;
    float memory_factor;
} TraitStats;

typedef struct Genome {
    float spread_weights[8];
    float spread_rate;
    float mutation_rate;
    float aggression;
    float resilience;
    float metabolism;
    
    float detection_range;
    uint8_t max_tracked;
    float social_factor;
    float merge_affinity;
    
    float nutrient_sensitivity;
    float toxin_sensitivity;
    float edge_affinity;
    float density_tolerance;
    float quorum_threshold;
    
    float toxin_production;
    float toxin_resistance;
    float signal_emission;
    float signal_sensitivity;
    float alarm_threshold;
    float gene_transfer_rate;
    
    float resource_consumption;
    float defense_priority;
    
    float dormancy_threshold;
    float dormancy_resistance;
    float sporulation_threshold;
    float biofilm_investment;
    float biofilm_tendency;
    float motility;
    float motility_direction;
    float specialization;
    
    float efficiency;
    
    float hidden_weights[8];
    float learning_rate;
    float memory_factor;
    
    Color body_color;
    Color border_color;
    
    CombatStats combat;
    GrowthStats growth;
    BehaviorStats behavior;
    TraitStats traits;
} Genome;

typedef struct {
    uint32_t colony_id;
    bool is_border;
    uint8_t age;
    int8_t component_id;
} Cell;

typedef enum {
    COLONY_STATE_NORMAL = 0,
    COLONY_STATE_DORMANT = 1,
    COLONY_STATE_STRESSED = 2,
} ColonyState;

typedef struct {
    uint32_t id;
    char name[64];
    Genome genome;
    size_t cell_count;
    size_t max_cell_count;
    uint64_t age;
    uint32_t parent_id;
    bool active;
    Color color;
    uint32_t shape_seed;
    float wobble_phase;
    float shape_evolution;
    
    ColonyState state;
    bool is_dormant;
    float stress_level;
    float biofilm_strength;
    float drift_x, drift_y;
    float signal_strength;
    
    float success_history[8];
    uint32_t last_population;
    
    uint32_t* cell_indices;
    size_t cell_indices_capacity;
    size_t cell_indices_count;
    
    float centroid_x;
    float centroid_y;
} Colony;

typedef struct {
    int width;
    int height;
    Cell* cells;
    Colony* colonies;
    size_t colony_count;
    size_t colony_capacity;
    uint64_t tick;
    atomic_uint next_colony_id;
    
    Colony** colony_by_id;
    size_t colony_by_id_capacity;
    
    float* nutrients;
    float* toxins;
    float* signals;
    float* alarm_signals;
    uint32_t* signal_source;
    uint32_t* alarm_source;
    
    float* scratch_signals;
    uint32_t* scratch_sources;
} World;

#endif
