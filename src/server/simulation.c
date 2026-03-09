#include "simulation.h"
#include "genetics.h"
#include "../shared/utils.h"
#include "../shared/names.h"
#include "../shared/colors.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Direction offsets for 4-connectivity (N, E, S, W)
static const int DX[] = {0, 1, 0, -1};
static const int DY[] = {-1, 0, 1, 0};

// Environmental constants
#define NUTRIENT_DEPLETION_RATE 0.05f   // Nutrients consumed per cell per tick
#define NUTRIENT_REGEN_RATE 0.002f      // Natural nutrient regeneration
#define TOXIN_DECAY_RATE 0.01f          // Toxin decay per tick
#define SIGNAL_DECAY_RATE 0.06f         // Signal decay per tick
#define ALARM_DECAY_RATE 0.10f          // Alarm decay per tick
#define QUORUM_SENSING_RADIUS 3         // Radius for local density calculation

typedef struct ColonyBehaviorInputs {
    float avg_nutrient;
    float avg_toxin;
    float avg_signal;
    float avg_alarm;
    float border_ratio;
    float pressure;
    float momentum;
    float growth_trend;
    int pop_delta;
} ColonyBehaviorInputs;

static float behavior_soft_clamp(float value) {
    return utils_clamp_f(0.5f + 0.5f * tanhf(value), 0.0f, 1.0f);
}

static float behavior_edge_seek(float edge_affinity) {
    return utils_clamp_f(0.5f + edge_affinity * 0.5f, 0.0f, 1.0f);
}

static void behavior_pick_top_two_channels(const float* values,
                                           int count,
                                           uint8_t* out_first_index,
                                           float* out_first_value,
                                           uint8_t* out_second_index,
                                           float* out_second_value) {
    uint8_t first_index = 0;
    uint8_t second_index = 0;
    float first_value = (count > 0) ? values[0] : 0.0f;
    float second_value = -1.0f;

    for (int i = 1; i < count; i++) {
        if (values[i] > first_value) {
            second_value = first_value;
            second_index = first_index;
            first_value = values[i];
            first_index = (uint8_t)i;
        } else if (values[i] > second_value) {
            second_value = values[i];
            second_index = (uint8_t)i;
        }
    }

    if (count <= 1) {
        second_index = first_index;
        second_value = first_value;
    }

    if (out_first_index) *out_first_index = first_index;
    if (out_first_value) *out_first_value = first_value;
    if (out_second_index) *out_second_index = second_index;
    if (out_second_value) *out_second_value = second_value;
}

static int behavior_pick_focus_direction(const Colony* colony) {
    int best_dir = 0;
    float best_score = -1.0f;

    for (int d = 0; d < DIR_COUNT; d++) {
        float angle = (float)d * ((float)M_PI / 4.0f);
        float angle_bias = 0.5f + 0.5f * cosf(angle - colony->genome.motility_direction);
        float score = colony->genome.spread_weights[d] * (0.55f + colony->success_history[d] * 0.35f);
        score += angle_bias * colony->behavior_actions[COLONY_ACTION_MOTILITY] * 0.45f;
        score += colony->behavior_actions[COLONY_ACTION_EXPAND] * 0.10f;
        if (score > best_score) {
            best_score = score;
            best_dir = d;
        }
    }

    return best_dir;
}

static void simulation_update_colony_behavior_graph(Colony* colony, const ColonyBehaviorInputs* inputs) {
    const Genome* genome = &colony->genome;
    float nutrient_need = utils_clamp_f((0.55f - inputs->avg_nutrient) * 2.0f, 0.0f, 1.0f);
    float toxin_threat = utils_clamp_f(inputs->avg_toxin * (0.6f + genome->toxin_sensitivity * 0.6f), 0.0f, 1.0f);
    float alarm_threat = utils_clamp_f(inputs->avg_alarm * 1.6f, 0.0f, 1.0f);
    float hostile_pressure = utils_clamp_f(inputs->pressure * 0.35f, 0.0f, 1.0f);
    float social_feedback = utils_clamp_f(inputs->avg_signal * (0.5f + genome->signal_sensitivity * 0.5f), 0.0f, 1.0f);
    float frontier_opportunity = utils_clamp_f(inputs->border_ratio * 1.8f, 0.0f, 1.0f);
    float edge_seek = behavior_edge_seek(genome->edge_affinity);
    float raw_sensors[COLONY_SENSOR_COUNT] = {
        inputs->avg_nutrient,
        toxin_threat,
        social_feedback,
        alarm_threat,
        frontier_opportunity,
        hostile_pressure,
        inputs->momentum,
        inputs->growth_trend,
    };
    float centered_sensors[COLONY_SENSOR_COUNT];
    float centered_drives[COLONY_DRIVE_COUNT];

    for (int sensor = 0; sensor < COLONY_SENSOR_COUNT; sensor++) {
        colony->behavior_sensors[sensor] = utils_clamp_f(raw_sensors[sensor] * genome->behavior_sensor_gains[sensor], 0.0f, 1.0f);
        centered_sensors[sensor] = (raw_sensors[sensor] * 2.0f - 1.0f) * genome->behavior_sensor_gains[sensor];
    }

    for (int drive = 0; drive < COLONY_DRIVE_COUNT; drive++) {
        float input = genome->behavior_drive_biases[drive];
        for (int sensor = 0; sensor < COLONY_SENSOR_COUNT; sensor++) {
            input += centered_sensors[sensor] * genome->behavior_drive_weights[drive][sensor];
        }

        switch (drive) {
            case COLONY_DRIVE_GROWTH:
                input += genome->spread_rate * 0.25f + genome->metabolism * 0.20f - colony->stress_level * 0.35f;
                break;
            case COLONY_DRIVE_CAUTION:
                input += colony->stress_level * 0.85f + genome->defense_priority * 0.20f;
                break;
            case COLONY_DRIVE_HOSTILITY:
                input += genome->aggression * 0.25f + genome->specialization * 0.15f - genome->merge_affinity * 0.15f;
                break;
            case COLONY_DRIVE_COHESION:
                input += genome->signal_emission * 0.20f + genome->merge_affinity * 0.20f + genome->social_factor * 0.15f;
                break;
            case COLONY_DRIVE_EXPLORATION:
                input += genome->motility * 0.25f + edge_seek * 0.10f + nutrient_need * 0.10f;
                break;
            case COLONY_DRIVE_PRESERVATION:
                input += colony->stress_level * 0.75f + genome->dormancy_resistance * 0.20f + genome->biofilm_tendency * 0.15f;
                break;
            default:
                break;
        }

        colony->behavior_drives[drive] = behavior_soft_clamp(input);
        centered_drives[drive] = colony->behavior_drives[drive] * 2.0f - 1.0f;
    }

    for (int action = 0; action < COLONY_ACTION_COUNT; action++) {
        float input = genome->behavior_action_biases[action];
        for (int drive = 0; drive < COLONY_DRIVE_COUNT; drive++) {
            input += centered_drives[drive] * genome->behavior_action_weights[action][drive];
        }

        switch (action) {
            case COLONY_ACTION_EXPAND:
                input += genome->spread_rate * 0.20f;
                break;
            case COLONY_ACTION_ATTACK:
                input += genome->aggression * 0.20f;
                break;
            case COLONY_ACTION_DEFEND:
                input += genome->defense_priority * 0.20f;
                break;
            case COLONY_ACTION_SIGNAL:
                input += genome->signal_emission * 0.20f;
                break;
            case COLONY_ACTION_TRANSFER:
                input += utils_clamp_f(genome->gene_transfer_rate * 10.0f, 0.0f, 1.0f) * 0.20f;
                break;
            case COLONY_ACTION_DORMANCY:
                input += colony->stress_level * 0.35f;
                break;
            case COLONY_ACTION_MOTILITY:
                input += genome->motility * 0.20f;
                break;
            default:
                break;
        }

        colony->behavior_actions[action] = behavior_soft_clamp(input);
    }

    if (colony->is_dormant || colony->behavior_actions[COLONY_ACTION_DORMANCY] > 0.72f) {
        colony->behavior_mode = COLONY_BEHAVIOR_MODE_DORMANT;
    } else if (colony->behavior_actions[COLONY_ACTION_ATTACK] > 0.58f &&
               colony->behavior_actions[COLONY_ACTION_ATTACK] >= colony->behavior_actions[COLONY_ACTION_DEFEND]) {
        colony->behavior_mode = COLONY_BEHAVIOR_MODE_RAIDING;
    } else if (colony->behavior_actions[COLONY_ACTION_DEFEND] > 0.58f) {
        colony->behavior_mode = COLONY_BEHAVIOR_MODE_FORTIFYING;
    } else if (colony->behavior_actions[COLONY_ACTION_SIGNAL] > 0.56f &&
               colony->behavior_drives[COLONY_DRIVE_COHESION] >= colony->behavior_drives[COLONY_DRIVE_HOSTILITY]) {
        colony->behavior_mode = COLONY_BEHAVIOR_MODE_COOPERATING;
    } else if (colony->behavior_actions[COLONY_ACTION_EXPAND] > 0.56f) {
        colony->behavior_mode = COLONY_BEHAVIOR_MODE_EXPANDING;
    } else if (colony->behavior_drives[COLONY_DRIVE_PRESERVATION] > colony->behavior_drives[COLONY_DRIVE_GROWTH]) {
        colony->behavior_mode = COLONY_BEHAVIOR_MODE_SURVIVAL;
    } else {
        colony->behavior_mode = COLONY_BEHAVIOR_MODE_BALANCED;
    }

    colony->focus_direction = (int8_t)behavior_pick_focus_direction(colony);
    behavior_pick_top_two_channels(colony->behavior_sensors,
                                   COLONY_SENSOR_COUNT,
                                   &colony->dominant_sensor,
                                   &colony->dominant_sensor_value,
                                   &colony->secondary_sensor,
                                   &colony->secondary_sensor_value);
    behavior_pick_top_two_channels(colony->behavior_drives,
                                   COLONY_DRIVE_COUNT,
                                   &colony->dominant_drive,
                                   &colony->dominant_drive_value,
                                   &colony->secondary_drive,
                                   &colony->secondary_drive_value);
}

static float vector_alignment(float ax, float ay, float bx, float by) {
    float alen = sqrtf(ax * ax + ay * ay);
    float blen = sqrtf(bx * bx + by * by);
    if (alen < 0.0001f || blen < 0.0001f) {
        return 0.0f;
    }

    return (ax * bx + ay * by) / (alen * blen);
}

static bool cell_is_border(World* world, int x, int y, uint32_t colony_id) {
    for (int d = 0; d < 4; d++) {
        Cell* neighbor = world_get_cell(world, x + DX[d], y + DY[d]);
        if (!neighbor || neighbor->colony_id != colony_id) {
            return true;
        }
    }

    return false;
}

static void emit_layer_signal(
    float* layer,
    uint32_t* source,
    int width,
    int height,
    int x,
    int y,
    uint32_t colony_id,
    float strength
) {
    if (!layer || strength <= 0.0f) {
        return;
    }

    for (int oy = -1; oy <= 1; oy++) {
        for (int ox = -1; ox <= 1; ox++) {
            int nx = x + ox;
            int ny = y + oy;
            if (nx < 0 || nx >= width || ny < 0 || ny >= height) {
                continue;
            }

            float falloff = 0.35f;
            if (ox == 0 && oy == 0) {
                falloff = 1.0f;
            } else if ((ox == 0) ^ (oy == 0)) {
                falloff = 0.55f;
            }

            int idx = ny * width + nx;
            float emitted = strength * falloff;
            float previous = layer[idx];
            layer[idx] = utils_clamp_f(previous + emitted, 0.0f, 1.0f);
            if (source && (source[idx] == 0 || source[idx] == colony_id || previous < emitted)) {
                source[idx] = colony_id;
            }
        }
    }
}

// Calculate local population density around a cell
static float calculate_local_density(World* world, int x, int y, uint32_t colony_id) {
    int count = 0;
    int total = 0;
    for (int dy = -QUORUM_SENSING_RADIUS; dy <= QUORUM_SENSING_RADIUS; dy++) {
        for (int dx = -QUORUM_SENSING_RADIUS; dx <= QUORUM_SENSING_RADIUS; dx++) {
            Cell* neighbor = world_get_cell(world, x + dx, y + dy);
            if (neighbor) {
                total++;
                if (neighbor->colony_id == colony_id) {
                    count++;
                }
            }
        }
    }
    return total > 0 ? (float)count / (float)total : 0.0f;
}

// Calculate environmental spread modifier for a target cell
static float calculate_env_spread_modifier(World* world, Colony* colony, int tx, int ty, int sx, int sy) {
    int target_idx = ty * world->width + tx;
    float modifier = 1.0f;
    
    // Chemotaxis: prefer higher nutrient levels
    float nutrient = world->nutrients[target_idx];
    modifier *= (1.0f + colony->genome.nutrient_sensitivity * (nutrient - 0.5f));
    
    // Toxin avoidance: avoid areas with toxins
    float toxin = world->toxins[target_idx];
    modifier *= (1.0f - colony->genome.toxin_sensitivity * toxin);
    
    // Edge preference: positive = prefer edges, negative = prefer center
    float edge_dist_x = fminf((float)tx, (float)(world->width - 1 - tx)) / (float)(world->width / 2);
    float edge_dist_y = fminf((float)ty, (float)(world->height - 1 - ty)) / (float)(world->height / 2);
    float edge_factor = 1.0f - fminf(edge_dist_x, edge_dist_y);
    modifier *= (1.0f + colony->genome.edge_affinity * (edge_factor - 0.5f));
    
    // Quorum sensing: reduce spread probability if local density exceeds threshold
    float local_density = calculate_local_density(world, sx, sy, colony->id);
    if (local_density > colony->genome.quorum_threshold) {
        float density_penalty = (local_density - colony->genome.quorum_threshold) * 
                                (1.0f - colony->genome.density_tolerance);
        modifier *= (1.0f - density_penalty);
    }

    if (world->signals && world->signal_source && world->signal_source[target_idx] != 0) {
        float signal = world->signals[target_idx];
        if (world->signal_source[target_idx] == colony->id) {
            modifier *= (1.0f + colony->genome.signal_sensitivity * signal * 0.25f);
        } else {
            modifier *= (1.0f - colony->genome.signal_sensitivity * signal * 0.12f * (1.0f - colony->genome.merge_affinity));
        }
    }

    if (world->alarm_signals && world->alarm_source && world->alarm_signals[target_idx] > 0.001f) {
        float alarm = world->alarm_signals[target_idx];
        float alarm_response = colony->genome.defense_priority - colony->genome.aggression;
        if (world->alarm_source[target_idx] == colony->id) {
            modifier *= (1.0f - alarm * alarm_response * 0.20f);
        } else {
            modifier *= (1.0f - alarm * (0.18f + colony->genome.signal_sensitivity * 0.22f));
        }
    }

    if (colony->is_dormant || colony->state == COLONY_STATE_DORMANT) {
        modifier *= 0.2f + nutrient * (0.45f + colony->genome.dormancy_resistance * 0.2f);
    }

    modifier *= (0.75f + colony->behavior_actions[COLONY_ACTION_EXPAND] * 0.55f);
    modifier *= (1.0f - colony->behavior_actions[COLONY_ACTION_DORMANCY] * 0.45f);
    modifier *= (0.9f + colony->behavior_actions[COLONY_ACTION_SIGNAL] * 0.15f);

    modifier *= (1.0f - colony->biofilm_strength * colony->genome.biofilm_investment * 0.10f);

    float drift_alignment = vector_alignment((float)(tx - sx), (float)(ty - sy), colony->drift_x, colony->drift_y);
    modifier *= (1.0f + drift_alignment * colony->genome.motility * 0.35f);
    
    return utils_clamp_f(modifier, 0.3f, 2.0f);  // Minimum floor of 0.3 to prevent stalling
}

// Stack for iterative flood-fill
typedef struct {
    int* data;
    int top;
    int capacity;
} Stack;

static Stack* stack_create(int capacity) {
    Stack* s = (Stack*)malloc(sizeof(Stack));
    if (!s) return NULL;
    s->data = (int*)malloc(capacity * 2 * sizeof(int));  // x,y pairs
    s->top = 0;
    s->capacity = capacity * 2;
    return s;
}

static void stack_destroy(Stack* s) {
    if (s) {
        free(s->data);
        free(s);
    }
}

static void stack_push(Stack* s, int x, int y) {
    if (s->top + 2 > s->capacity) {
        int new_capacity = s->capacity * 2;
        int* new_data = (int*)realloc(s->data, new_capacity * sizeof(int));
        if (!new_data) {
            // Realloc failed - cannot push, this will cause flood-fill to be incomplete
            // but avoids a crash. The caller should handle incomplete results.
            return;
        }
        s->data = new_data;
        s->capacity = new_capacity;
    }
    s->data[s->top++] = x;
    s->data[s->top++] = y;
}

static bool stack_pop(Stack* s, int* x, int* y) {
    if (s->top < 2) return false;
    *y = s->data[--s->top];
    *x = s->data[--s->top];
    return true;
}

static bool stack_empty(Stack* s) {
    return s->top == 0;
}

// Flood-fill from a starting cell, marking all connected cells with component_id
static int flood_fill(World* world, int start_x, int start_y, uint32_t colony_id, int8_t comp_id) {
    Stack* stack = stack_create(world->width * world->height / 4);
    if (!stack) return 0;

    const int width = world->width;
    const int height = world->height;
    Cell* cells = world->cells;

    int count = 0;
    stack_push(stack, start_x, start_y);

    cells[start_y * width + start_x].component_id = comp_id;
    
    while (!stack_empty(stack)) {
        int x, y;
        if (!stack_pop(stack, &x, &y)) {
            break;
        }
        count++;
        
        // Check all 4 neighbors
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d];
            int ny = y + DY[d];

            if (nx < 0 || nx >= width || ny < 0 || ny >= height) {
                continue;
            }

            Cell* neighbor = &cells[ny * width + nx];
            if (neighbor->colony_id == colony_id && neighbor->component_id == -1) {
                neighbor->component_id = comp_id;
                stack_push(stack, nx, ny);
            }
        }
    }
    
    stack_destroy(stack);
    return count;
}

int* find_connected_components(World* world, uint32_t colony_id, int* num_components) {
    if (!world || !num_components || colony_id == 0) {
        if (num_components) *num_components = 0;
        return NULL;
    }
    
    int width = world->width;
    int height = world->height;
    int total = width * height;
    Cell* cells = world->cells;

    // Reset component markers for this colony's cells
    for (int i = 0; i < total; i++) {
        if (cells[i].colony_id == colony_id) {
            cells[i].component_id = -1;
        }
    }
    
    // Find components
    // Note: component_id is int8_t (-128 to 127), so we can track at most 127 components (0-126)
    // In practice, colonies rarely have more than a few components
    const int MAX_COMPONENTS = 127;
    int* sizes = NULL;
    int count = 0;
    int capacity = 4;
    sizes = (int*)malloc(capacity * sizeof(int));
    if (!sizes) {
        *num_components = 0;
        return NULL;
    }
    
    for (int y = 0; y < height; y++) {
        int row_base = y * width;
        for (int x = 0; x < width; x++) {
            Cell* cell = &cells[row_base + x];
            if (cell->colony_id == colony_id && cell->component_id == -1) {
                // Start new component - but stop if we hit the int8_t limit
                if (count >= MAX_COMPONENTS) {
                    // Too many components to track safely, return what we have
                    // Remaining cells will be processed on next tick
                    *num_components = count;
                    return sizes;
                }
                if (count >= capacity) {
                    int new_capacity = capacity * 2;
                    if (new_capacity > MAX_COMPONENTS) new_capacity = MAX_COMPONENTS;
                    int* new_sizes = (int*)realloc(sizes, new_capacity * sizeof(int));
                    if (!new_sizes) {
                        // Realloc failed - return what we have
                        *num_components = count;
                        return sizes;
                    }
                    sizes = new_sizes;
                    capacity = new_capacity;
                }
                sizes[count] = flood_fill(world, x, y, colony_id, (int8_t)count);
                count++;
            }
        }
    }
    
    *num_components = count;
    return sizes;
}

// Count friendly neighbors around a cell (for flanking calculation)
static int count_friendly_neighbors(World* world, int x, int y, uint32_t colony_id) {
    int count = 0;
    for (int d = 0; d < 4; d++) {
        Cell* n = world_get_cell(world, x + DX[d], y + DY[d]);
        if (n && n->colony_id == colony_id) count++;
    }
    return count;
}

// Count enemy neighbors around a cell (for pressure calculation)
static int count_enemy_neighbors(World* world, int x, int y, uint32_t colony_id) {
    int count = 0;
    for (int d = 0; d < 4; d++) {
        Cell* n = world_get_cell(world, x + DX[d], y + DY[d]);
        if (n && n->colony_id != 0 && n->colony_id != colony_id) count++;
    }
    return count;
}

// Get directional weight for spread_weights (maps 4-direction to 8-direction weights)
static float get_direction_weight(Genome* g, int dx, int dy) {
    // Map dx,dy to direction index
    // N=0, NE=1, E=2, SE=3, S=4, SW=5, W=6, NW=7
    int dir = -1;
    if (dy == -1 && dx == 0) dir = 0;      // N
    else if (dy == 0 && dx == 1) dir = 2;  // E
    else if (dy == 1 && dx == 0) dir = 4;  // S
    else if (dy == 0 && dx == -1) dir = 6; // W
    return dir >= 0 ? g->spread_weights[dir] : 0.5f;
}

void simulation_spread(World* world) {
    if (!world) return;
    
    // Create list of cells to colonize (avoid modifying while iterating)
    typedef struct { int x, y; uint32_t colony_id; } PendingCell;
    PendingCell* pending = NULL;
    int pending_count = 0;
    int pending_capacity = 64;
    pending = (PendingCell*)malloc(pending_capacity * sizeof(PendingCell));
    if (!pending) return;
    
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            Cell* cell = world_get_cell(world, x, y);
            if (!cell || cell->colony_id == 0) continue;
            
            Colony* colony = world_get_colony(world, cell->colony_id);
            if (!colony) continue;
            
            // Try to spread to neighbors based on spread_rate with environmental modifiers
            for (int d = 0; d < 4; d++) {
                int nx = x + DX[d];
                int ny = y + DY[d];
                
                Cell* neighbor = world_get_cell(world, nx, ny);
                if (!neighbor) continue;
                
                if (neighbor->colony_id == 0) {
                    // Empty cell - calculate spread probability with environmental sensing
                    float env_modifier = calculate_env_spread_modifier(world, colony, nx, ny, x, y);
                    
                    // Directional preference from genome
                    float dir_weight = get_direction_weight(&colony->genome, DX[d], DY[d]);
                    
                    // Strategic spread: push harder towards open space, less where enemies are
                    int enemy_count = count_enemy_neighbors(world, nx, ny, cell->colony_id);
                    float strategic_modifier = 1.0f;
                    if (enemy_count > 0) {
                        // Slow down if target is contested (unless very aggressive)
                        strategic_modifier *= (0.3f + colony->genome.aggression * 0.4f);
                    }
                    
                    // Success history affects spread direction
                    float history_bonus = 1.0f + colony->success_history[d % 8] * 0.2f;
                    
                    // More active spread to keep colonies dynamic
                    float spread_prob = colony->genome.spread_rate * colony->genome.metabolism * 
                                        env_modifier * dir_weight * strategic_modifier * history_bonus;
                    
                    if (rand_float() < spread_prob) {
                        if (pending_count >= pending_capacity) {
                            pending_capacity *= 2;
                            pending = (PendingCell*)realloc(pending, pending_capacity * sizeof(PendingCell));
                        }
                        pending[pending_count++] = (PendingCell){nx, ny, cell->colony_id};
                    }
                }
            }
        }
    }
    
    // Apply pending colonizations - this is where new cells are "born"
    // Mutations happen during cell division (new cell creation)
    for (int i = 0; i < pending_count; i++) {
        Cell* cell = world_get_cell(world, pending[i].x, pending[i].y);
        if (cell) {
            uint32_t old_colony = cell->colony_id;
            
            // Update old colony's cell count
            if (old_colony != 0) {
                Colony* old = world_get_colony(world, old_colony);
                if (old && old->cell_count > 0) {
                    old->cell_count--;
                }
            }
            
            // Colonize
            cell->colony_id = pending[i].colony_id;
            cell->age = 0;
            
            // Update new colony's cell count and potentially mutate
            Colony* colony = world_get_colony(world, pending[i].colony_id);
            if (colony) {
                colony->cell_count++;
                
                // MUTATION ON REPRODUCTION: Each new cell has a chance to cause colony mutation
                // Higher stress = more mutations (adaptation pressure)
                float mutation_chance = colony->genome.mutation_rate * 
                                        (1.0f + colony->stress_level * 2.0f);
                if (rand_float() < mutation_chance) {
                    genome_mutate(&colony->genome);
                }
            }
        }
    }
    
    free(pending);
}

void simulation_mutate(World* world) {
    // Mutations now primarily happen during cell division (in simulation_spread)
    // This function provides a small baseline mutation for all colonies
    // to ensure even stable colonies can adapt over very long time
    if (!world) return;
    
    for (size_t i = 0; i < world->colony_count; i++) {
        Colony* colony = &world->colonies[i];
        if (!colony->active) continue;
        
        // Very low baseline mutation rate (most mutation comes from cell division)
        // Stressed colonies mutate more as they try to adapt
        float baseline_rate = colony->genome.mutation_rate * 0.1f;
        baseline_rate *= (1.0f + colony->stress_level);
        
        if (rand_float() < baseline_rate) {
            genome_mutate(&colony->genome);
        }
    }
}

void simulation_check_divisions(World* world) {
    if (!world) return;
    
    // Only process one division per tick to keep simulation stable
    bool division_occurred = false;
    
    for (size_t i = 0; i < world->colony_count && !division_occurred; i++) {
        Colony* colony = &world->colonies[i];
        if (!colony->active || colony->cell_count < 2) continue;
        
        int num_components;
        int* sizes = find_connected_components(world, colony->id, &num_components);
        
        if (sizes && num_components > 1) {
            uint32_t parent_id = colony->id;
            Genome parent_genome = colony->genome;
            uint32_t parent_shape_seed = colony->shape_seed;

            // Colony has split! Keep largest component, create new colonies for others
            int largest_idx = 0;
            int largest_size = sizes[0];
            for (int c = 1; c < num_components; c++) {
                if (sizes[c] > largest_size) {
                    largest_size = sizes[c];
                    largest_idx = c;
                }
            }
            
            uint32_t* component_new_ids = (uint32_t*)calloc((size_t)num_components, sizeof(uint32_t));
            if (!component_new_ids) {
                free(sizes);
                return;
            }

            // Create new colonies for non-largest components (min 5 cells to avoid tiny fragments)
            for (int c = 0; c < num_components; c++) {
                if (c == largest_idx) continue;
                if (sizes[c] < 5) {
                    continue;
                }
                
                // Create new colony with mutated genome
                Colony new_colony;
                memset(&new_colony, 0, sizeof(Colony));
                new_colony.id = 0;
                generate_scientific_name(new_colony.name, sizeof(new_colony.name));
                new_colony.genome = parent_genome;
                genome_mutate(&new_colony.genome);
                new_colony.color = new_colony.genome.body_color;
                new_colony.cell_count = (size_t)sizes[c];
                new_colony.max_cell_count = (size_t)sizes[c];
                new_colony.active = true;
                new_colony.age = 0;
                new_colony.parent_id = parent_id;
                
                // Generate unique shape seed for procedural shape (inherit and mutate from parent)
                new_colony.shape_seed = parent_shape_seed ^ (uint32_t)rand() ^ ((uint32_t)rand() << 8);
                new_colony.wobble_phase = (float)(rand() % 628) / 100.0f;
                
                uint32_t new_id = world_add_colony(world, new_colony);
                component_new_ids[c] = new_id;
            }

            // Single pass: reassign non-largest components or orphan tiny fragments
            int total_cells = world->width * world->height;
            for (int j = 0; j < total_cells; j++) {
                Cell* cell = &world->cells[j];
                if (cell->colony_id != parent_id) {
                    continue;
                }

                int comp = cell->component_id;
                if (comp == largest_idx) {
                    continue;
                }

                if (comp < 0 || comp >= num_components) {
                    continue;
                }

                uint32_t new_id = component_new_ids[comp];
                if (new_id != 0) {
                    cell->colony_id = new_id;
                } else {
                    cell->colony_id = 0;
                    cell->age = 0;
                    cell->is_border = false;
                }
            }

            free(component_new_ids);
            
            // Update original colony's cell count to largest component only
            Colony* parent_colony = world_get_colony(world, parent_id);
            if (parent_colony) {
                parent_colony->cell_count = (size_t)largest_size;
            }
            division_occurred = true;  // Only one division per tick
        }
        
        free(sizes);
    }
}

void simulation_check_recombinations(World* world) {
    if (!world) return;
    
    // Check for adjacent compatible colonies
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            Cell* cell = world_get_cell(world, x, y);
            if (!cell || cell->colony_id == 0) continue;
            
            Colony* colony_a = world_get_colony(world, cell->colony_id);
            if (!colony_a) continue;
            
            // Check right and down neighbors only (avoid double-checking)
            for (int d = 1; d <= 2; d++) {
                int nx = x + DX[d];
                int ny = y + DY[d];
                
                Cell* neighbor = world_get_cell(world, nx, ny);
                if (!neighbor || neighbor->colony_id == 0 || neighbor->colony_id == cell->colony_id) continue;
                
                Colony* colony_b = world_get_colony(world, neighbor->colony_id);
                if (!colony_b) continue;
                
                // Recombination only happens between very closely related colonies
                // (e.g., recently divided colonies that reconnect)
                // This requires checking parent_id relationship
                if (colony_a->parent_id != colony_b->id && colony_b->parent_id != colony_a->id) {
                    // Not parent-child, also check if siblings (same parent)
                    if (colony_a->parent_id == 0 || colony_a->parent_id != colony_b->parent_id) {
                        continue;  // Not related, no merge
                    }
                }
                
                // Calculate genetic distance - must be very close for siblings to merge
                float distance = genome_distance(&colony_a->genome, &colony_b->genome);
                
                // Very strict threshold - only nearly identical genomes merge
                float threshold = 0.05f;
                
                // Apply merge_affinity bonus: average of both colonies' affinities
                float avg_affinity = (colony_a->genome.merge_affinity + colony_b->genome.merge_affinity) / 2.0f;
                threshold += avg_affinity * 0.1f;  // Max bonus of 0.03
                
                // Check genetic compatibility with adjusted threshold
                if (distance <= threshold) {
                    // Merge: smaller colony joins larger
                    Colony* larger = colony_a->cell_count >= colony_b->cell_count ? colony_a : colony_b;
                    Colony* smaller = colony_a->cell_count >= colony_b->cell_count ? colony_b : colony_a;
                    
                    // Merge genomes
                    larger->genome = genome_merge(&larger->genome, larger->cell_count,
                                                  &smaller->genome, smaller->cell_count);
                    
                    // Transfer cells
                    for (int j = 0; j < world->width * world->height; j++) {
                        if (world->cells[j].colony_id == smaller->id) {
                            world->cells[j].colony_id = larger->id;
                        }
                    }
                    
                    larger->cell_count += smaller->cell_count;
                    smaller->cell_count = 0;
                    smaller->active = false;
                    
                    return;  // Only one merge per tick to keep things stable
                }
            }
        }
    }
}

// ============================================================================
// Parallel/Region-based processing functions
// ============================================================================

PendingBuffer* pending_buffer_create(int initial_capacity) {
    PendingBuffer* buf = (PendingBuffer*)malloc(sizeof(PendingBuffer));
    if (!buf) return NULL;
    
    buf->cells = (PendingCell*)malloc(initial_capacity * sizeof(PendingCell));
    if (!buf->cells) {
        free(buf);
        return NULL;
    }
    buf->count = 0;
    buf->capacity = initial_capacity;
    return buf;
}

void pending_buffer_destroy(PendingBuffer* buf) {
    if (buf) {
        free(buf->cells);
        free(buf);
    }
}

void pending_buffer_add(PendingBuffer* buf, int x, int y, uint32_t colony_id) {
    if (!buf) return;
    
    if (buf->count >= buf->capacity) {
        buf->capacity *= 2;
        buf->cells = (PendingCell*)realloc(buf->cells, buf->capacity * sizeof(PendingCell));
        if (!buf->cells) return;
    }
    buf->cells[buf->count].x = x;
    buf->cells[buf->count].y = y;
    buf->cells[buf->count].colony_id = colony_id;
    buf->count++;
}

void pending_buffer_clear(PendingBuffer* buf) {
    if (buf) buf->count = 0;
}

void simulation_spread_region(World* world, int start_x, int start_y, 
                              int end_x, int end_y, PendingBuffer* pending) {
    if (!world || !pending) return;
    
    for (int y = start_y; y < end_y; y++) {
        for (int x = start_x; x < end_x; x++) {
            Cell* cell = world_get_cell(world, x, y);
            if (!cell || cell->colony_id == 0) continue;
            
            Colony* colony = world_get_colony(world, cell->colony_id);
            if (!colony) continue;
            
            // Try to spread to neighbors based on spread_rate
            for (int d = 0; d < 4; d++) {
                int nx = x + DX[d];
                int ny = y + DY[d];
                
                Cell* neighbor = world_get_cell(world, nx, ny);
                if (!neighbor) continue;
                
                if (neighbor->colony_id == 0) {
                    // Empty cell - can spread (increased base rate)
                    float spread_chance = colony->genome.spread_rate * colony->genome.metabolism * 1.5f;
                    if (rand_float() < spread_chance) {
                        pending_buffer_add(pending, nx, ny, cell->colony_id);
                    }
                } else if (neighbor->colony_id != cell->colony_id) {
                    // Enemy cell - might overtake based on aggression vs resilience
                    Colony* enemy = world_get_colony(world, neighbor->colony_id);
                    if (enemy && enemy->active) {
                        // More aggressive combat: attacker advantage
                        float attack = colony->genome.aggression * (1.0f + colony->genome.toxin_production * 0.5f);
                        float defense = enemy->genome.resilience * (0.5f + enemy->genome.defense_priority * 0.5f);
                        float combat_chance = attack / (attack + defense + 0.1f);
                        // High combat rate for active borders
                        if (rand_float() < combat_chance * 0.85f) {
                            pending_buffer_add(pending, nx, ny, cell->colony_id);
                        }
                    }
                }
            }
        }
    }
}

void simulation_apply_pending(World* world, PendingBuffer** buffers, int buffer_count) {
    if (!world || !buffers) return;
    
    for (int b = 0; b < buffer_count; b++) {
        PendingBuffer* pending = buffers[b];
        if (!pending) continue;
        
        for (int i = 0; i < pending->count; i++) {
            Cell* cell = world_get_cell(world, pending->cells[i].x, pending->cells[i].y);
            if (cell) {
                uint32_t old_colony = cell->colony_id;
                
                // Update old colony's cell count
                if (old_colony != 0) {
                    Colony* old = world_get_colony(world, old_colony);
                    if (old && old->cell_count > 0) {
                        old->cell_count--;
                    }
                }
                
                // Colonize
                cell->colony_id = pending->cells[i].colony_id;
                cell->age = 0;
                
                // Update new colony's cell count
                Colony* colony = world_get_colony(world, pending->cells[i].colony_id);
                if (colony) {
                    colony->cell_count++;
                }
            }
        }
    }
}

void simulation_mutate_region(World* world, int start_x, int start_y, 
                              int end_x, int end_y) {
    (void)start_x; (void)start_y; (void)end_x; (void)end_y;
    // Mutations happen per-colony, not per-cell, so region doesn't matter
    // This is kept for API consistency; actual mutation happens in simulation_mutate
    if (!world) return;
}

void simulation_age_region(World* world, int start_x, int start_y, 
                           int end_x, int end_y) {
    if (!world) return;
    
    for (int y = start_y; y < end_y; y++) {
        for (int x = start_x; x < end_x; x++) {
            int idx = y * world->width + x;
            if (world->cells[idx].colony_id != 0 && world->cells[idx].age < 255) {
                world->cells[idx].age++;
            }
        }
    }
}

void simulation_recount_colony_cells(World* world) {
    if (!world) return;
    
    // First recount all cells from grid to ensure accuracy
    for (size_t i = 0; i < world->colony_count; i++) {
        world->colonies[i].cell_count = 0;
    }
    
    int total_cells = world->width * world->height;
    for (int j = 0; j < total_cells; j++) {
        uint32_t cid = world->cells[j].colony_id;
        if (cid == 0 || (size_t)cid >= world->colony_index_capacity) {
            continue;
        }

        uint32_t idx = world->colony_index_map[cid];
        if (idx == UINT32_MAX || idx >= world->colony_count) {
            continue;
        }

        Colony* col = &world->colonies[idx];
        if (col->id == cid && col->active) {
            col->cell_count++;
        }
    }
}

void simulation_update_colony_stats(World* world) {
    if (!world) return;

    simulation_recount_colony_cells(world);

    simulation_update_colony_derived(world);
}

void simulation_update_colony_derived(World* world) {
    if (!world) return;

    for (size_t i = 0; i < world->colony_count; i++) {
        Colony* colony = &world->colonies[i];
        if (!colony->active) continue;

        // Track max population
        if (colony->cell_count > colony->max_cell_count) {
            colony->max_cell_count = colony->cell_count;
        }

        // Mark as dead if population hits 0
        if (colony->cell_count == 0) {
            colony->active = false;
            continue;
        }

        // Animate wobble phase for organic movement
        colony->wobble_phase += 0.03f;
        if (colony->wobble_phase > 6.28318f) colony->wobble_phase -= 6.28318f;

        // Note: shape_seed is NOT mutated - that causes jarring visual jumps
        // Shape evolution happens naturally through smooth wobble_phase animation
    }
}

// ============================================================================
// Environmental Dynamics Functions
// ============================================================================

// Update nutrient levels: deplete where cells are, regenerate elsewhere
void simulation_update_nutrients(World* world) {
    if (!world || !world->nutrients) return;
    
    int total_cells = world->width * world->height;
    
    for (int i = 0; i < total_cells; i++) {
        if (world->cells[i].colony_id != 0) {
            // Cells consume nutrients based on metabolism
            Colony* colony = NULL;
            uint32_t cid = world->cells[i].colony_id;
            if ((size_t)cid < world->colony_index_capacity) {
                uint32_t idx = world->colony_index_map[cid];
                if (idx != UINT32_MAX && idx < world->colony_count) {
                    Colony* candidate = &world->colonies[idx];
                    if (candidate->id == cid && candidate->active) {
                        colony = candidate;
                    }
                }
            }

            float consumption = NUTRIENT_DEPLETION_RATE;
            if (colony) {
                consumption *= colony->genome.metabolism;
                // High efficiency reduces consumption
                consumption *= (1.0f - colony->genome.efficiency * 0.5f);
            }
            world->nutrients[i] = utils_clamp_f(world->nutrients[i] - consumption, 0.0f, 1.0f);
        } else {
            // Empty cells slowly regenerate nutrients
            world->nutrients[i] = utils_clamp_f(world->nutrients[i] + NUTRIENT_REGEN_RATE, 0.0f, 1.0f);
        }
    }
}

void simulation_update_behavior_layers(World* world) {
    if (!world) return;

    int total_cells = world->width * world->height;
    for (int i = 0; i < total_cells; i++) {
        if (world->nutrients && world->cells[i].colony_id == 0) {
            world->nutrients[i] = utils_clamp_f(world->nutrients[i] + NUTRIENT_REGEN_RATE * 1.5f, 0.0f, 1.0f);
        }

        if (world->toxins) {
            world->toxins[i] = utils_clamp_f(world->toxins[i] * (1.0f - TOXIN_DECAY_RATE * 1.5f), 0.0f, 1.0f);
        }

        if (world->signals) {
            world->signals[i] = utils_clamp_f(world->signals[i] * (1.0f - SIGNAL_DECAY_RATE), 0.0f, 1.0f);
            if (world->signal_source && world->signals[i] < 0.01f) {
                world->signal_source[i] = 0;
            }
        }

        if (world->alarm_signals) {
            world->alarm_signals[i] = utils_clamp_f(world->alarm_signals[i] * (1.0f - ALARM_DECAY_RATE), 0.0f, 1.0f);
            if (world->alarm_source && world->alarm_signals[i] < 0.01f) {
                world->alarm_source[i] = 0;
            }
        }
    }

    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            Cell* cell = world_get_cell(world, x, y);
            if (!cell || cell->colony_id == 0) continue;

            Colony* colony = world_get_colony(world, cell->colony_id);
            if (!colony || !colony->active) continue;

            int idx = y * world->width + x;
            int enemy_neighbors = count_enemy_neighbors(world, x, y, cell->colony_id);
            bool is_border = cell_is_border(world, x, y, cell->colony_id);
            cell->is_border = is_border;

            if (world->nutrients) {
                float nutrient_drain = NUTRIENT_DEPLETION_RATE * colony->genome.metabolism;
                nutrient_drain *= (0.65f + colony->genome.resource_consumption * 0.45f);
                nutrient_drain *= (1.0f - colony->genome.efficiency * 0.45f);
                if (colony->is_dormant) {
                    nutrient_drain *= 0.35f;
                }
                world->nutrients[idx] = utils_clamp_f(world->nutrients[idx] - nutrient_drain, 0.0f, 1.0f);
            }

            if (world->signals && world->signal_source) {
                float signal_emit = colony->genome.signal_emission * (is_border ? 0.08f : 0.03f);
                signal_emit *= (0.7f + colony->signal_strength * 0.6f + colony->genome.signal_sensitivity * 0.2f);
                signal_emit *= (0.7f + colony->behavior_actions[COLONY_ACTION_SIGNAL] * 0.6f);
                if (colony->is_dormant) {
                    signal_emit *= 0.45f;
                }
                emit_layer_signal(world->signals, world->signal_source, world->width, world->height,
                                  x, y, colony->id, signal_emit);
            }

            if (world->alarm_signals && world->alarm_source) {
                float alarm_emit = 0.0f;
                if (enemy_neighbors > 0 || colony->stress_level > colony->genome.alarm_threshold) {
                    alarm_emit = (0.04f + colony->stress_level * 0.08f + enemy_neighbors * 0.03f);
                    alarm_emit *= (0.5f + colony->genome.signal_emission * 0.5f);
                    alarm_emit *= (0.7f + colony->behavior_actions[COLONY_ACTION_DEFEND] * 0.7f);
                }
                emit_layer_signal(world->alarm_signals, world->alarm_source, world->width, world->height,
                                  x, y, colony->id, alarm_emit);
            }

            if (world->toxins && is_border && colony->genome.toxin_production > 0.01f) {
                float toxin_emit = colony->genome.toxin_production * (0.04f + colony->genome.defense_priority * 0.05f);
                if (colony->state == COLONY_STATE_STRESSED) {
                    toxin_emit *= 1.15f;
                }
                toxin_emit *= (0.75f + colony->behavior_actions[COLONY_ACTION_ATTACK] * 0.55f +
                               colony->behavior_actions[COLONY_ACTION_DEFEND] * 0.25f);
                emit_layer_signal(world->toxins, NULL, world->width, world->height,
                                  x, y, colony->id, toxin_emit);
            }
        }
    }
}

void simulation_update_colony_dynamics(World* world) {
    if (!world) return;

    size_t colony_count = world->colony_count;
    if (colony_count == 0) return;

    float* nutrient_sum = (float*)calloc(colony_count, sizeof(float));
    float* toxin_sum = (float*)calloc(colony_count, sizeof(float));
    float* own_signal_sum = (float*)calloc(colony_count, sizeof(float));
    float* alarm_sum = (float*)calloc(colony_count, sizeof(float));
    float* enemy_pressure = (float*)calloc(colony_count, sizeof(float));
    int* border_count = (int*)calloc(colony_count, sizeof(int));
    if (!nutrient_sum || !toxin_sum || !own_signal_sum || !alarm_sum || !enemy_pressure || !border_count) {
        free(nutrient_sum);
        free(toxin_sum);
        free(own_signal_sum);
        free(alarm_sum);
        free(enemy_pressure);
        free(border_count);
        return;
    }

    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            int cell_idx = y * world->width + x;
            Cell* cell = &world->cells[cell_idx];
            uint32_t colony_id = cell->colony_id;
            if (colony_id == 0 || (size_t)colony_id >= world->colony_index_capacity) {
                continue;
            }

            uint32_t colony_index = world->colony_index_map[colony_id];
            if (colony_index == UINT32_MAX || colony_index >= world->colony_count) {
                continue;
            }

            Colony* colony = &world->colonies[colony_index];
            if (!colony->active || colony->id != colony_id) {
                continue;
            }

            bool is_border = cell_is_border(world, x, y, colony_id);
            cell->is_border = is_border;
            if (is_border) {
                border_count[colony_index]++;
                enemy_pressure[colony_index] += (float)count_enemy_neighbors(world, x, y, colony_id);
            }

            if (world->nutrients) {
                nutrient_sum[colony_index] += world->nutrients[cell_idx];
            }
            if (world->toxins) {
                toxin_sum[colony_index] += world->toxins[cell_idx];
            }
            if (world->signals && world->signal_source && world->signal_source[cell_idx] == colony_id) {
                own_signal_sum[colony_index] += world->signals[cell_idx];
            }
            if (world->alarm_signals) {
                alarm_sum[colony_index] += world->alarm_signals[cell_idx];
            }
        }
    }

    for (size_t i = 0; i < colony_count; i++) {
        Colony* colony = &world->colonies[i];
        if (!colony->active) continue;

        if (colony->cell_count == 0) {
            colony->active = false;
            continue;
        }

        float population = (float)colony->cell_count;
        float avg_nutrient = nutrient_sum[i] / population;
        float avg_toxin = toxin_sum[i] / population;
        float avg_signal = own_signal_sum[i] / population;
        float avg_alarm = alarm_sum[i] / population;
        float border_ratio = (float)border_count[i] / population;
        float pressure = enemy_pressure[i] / population;
        float history_sum = 0.0f;
        for (int d = 0; d < DIR_COUNT; d++) {
            history_sum += colony->success_history[d];
        }
        float momentum = history_sum / (float)DIR_COUNT;
        int pop_delta = (int)colony->cell_count - (int)colony->last_population;
        float pop_scale = fmaxf(8.0f, population * 0.08f);
        float growth_trend = utils_clamp_f(0.5f + (float)pop_delta / pop_scale * 0.5f, 0.0f, 1.0f);

        float stress_delta = -0.006f - colony->genome.resilience * 0.008f;
        if (avg_nutrient < 0.45f) {
            stress_delta += (0.45f - avg_nutrient) * (0.12f + colony->genome.resource_consumption * 0.04f);
        }
        if (avg_toxin > 0.08f) {
            stress_delta += avg_toxin * (0.10f + (1.0f - colony->genome.toxin_resistance) * 0.08f);
        }
        if (pressure > 0.0f) {
            stress_delta += pressure * (0.02f + (1.0f - colony->genome.defense_priority) * 0.01f);
        }
        stress_delta -= avg_signal * colony->genome.signal_sensitivity * 0.02f;
        stress_delta -= colony->biofilm_strength * 0.01f;
        if (avg_nutrient > 0.65f) {
            stress_delta -= 0.01f;
        }

        colony->stress_level = utils_clamp_f(colony->stress_level + stress_delta, 0.0f, 1.0f);

        ColonyBehaviorInputs behavior_inputs = {
            .avg_nutrient = avg_nutrient,
            .avg_toxin = avg_toxin,
            .avg_signal = avg_signal,
            .avg_alarm = avg_alarm,
            .border_ratio = border_ratio,
            .pressure = pressure,
            .momentum = momentum,
            .growth_trend = growth_trend,
            .pop_delta = pop_delta,
        };
        simulation_update_colony_behavior_graph(colony, &behavior_inputs);

        float target_biofilm = colony->genome.biofilm_investment * (0.5f + colony->genome.biofilm_tendency * 0.5f);
        if (colony->stress_level > 0.35f || pressure > 0.15f) {
            target_biofilm += colony->genome.defense_priority * 0.2f;
        }
        target_biofilm += colony->behavior_actions[COLONY_ACTION_DEFEND] * 0.18f;
        target_biofilm += colony->behavior_actions[COLONY_ACTION_DORMANCY] * 0.08f;
        target_biofilm = utils_clamp_f(target_biofilm, 0.0f, 1.0f);
        if (colony->biofilm_strength < target_biofilm) {
            colony->biofilm_strength = utils_clamp_f(colony->biofilm_strength + 0.01f, 0.0f, 1.0f);
        } else {
            colony->biofilm_strength = utils_clamp_f(colony->biofilm_strength - 0.004f, 0.0f, 1.0f);
        }

        colony->signal_strength = utils_clamp_f(
            colony->genome.signal_emission * (0.25f + border_ratio * 0.5f + pressure * 0.2f) +
            colony->behavior_actions[COLONY_ACTION_SIGNAL] * 0.45f,
            0.0f,
            1.0f
        );

        for (int d = 0; d < 8; d++) {
            colony->success_history[d] *= (0.995f + colony->genome.memory_factor * 0.004f);
        }

        float desired_speed = colony->genome.motility * (0.35f + colony->behavior_actions[COLONY_ACTION_MOTILITY] * 0.85f);
        if (avg_nutrient < 0.35f) {
            desired_speed *= 1.15f;
        }
        if (colony->is_dormant) {
            desired_speed *= 0.3f;
        }
        float desired_dx = cosf(colony->genome.motility_direction) * desired_speed;
        float desired_dy = sinf(colony->genome.motility_direction) * desired_speed;
        colony->drift_x = colony->drift_x * 0.8f + desired_dx * 0.2f;
        colony->drift_y = colony->drift_y * 0.8f + desired_dy * 0.2f;

        float dormancy_trigger = fmaxf(0.25f, colony->genome.sporulation_threshold * 0.85f);
        if (colony->behavior_actions[COLONY_ACTION_DORMANCY] > 0.62f &&
            colony->stress_level > dormancy_trigger &&
            avg_nutrient < 0.45f &&
            colony->genome.dormancy_threshold > 0.2f) {
            colony->state = COLONY_STATE_DORMANT;
            colony->is_dormant = true;
            colony->stress_level = utils_clamp_f(colony->stress_level - colony->genome.dormancy_resistance * 0.015f, 0.0f, 1.0f);
        } else if (colony->behavior_actions[COLONY_ACTION_DEFEND] > 0.58f ||
                   colony->stress_level > 0.45f || avg_alarm > 0.12f || pressure > 0.12f) {
            colony->state = COLONY_STATE_STRESSED;
            colony->is_dormant = false;
        } else {
            colony->state = COLONY_STATE_NORMAL;
            colony->is_dormant = false;
        }

        if (colony->is_dormant) {
            colony->behavior_mode = COLONY_BEHAVIOR_MODE_DORMANT;
        }

        colony->last_population = (uint32_t)colony->cell_count;
        if (pop_delta < -3 && colony->genome.learning_rate > 0.3f) {
            int random_dir = colony->focus_direction >= 0 ? colony->focus_direction : (rand() % DIR_COUNT);
            colony->success_history[random_dir] = utils_clamp_f(
                colony->success_history[random_dir] + 0.08f * rand_float(), 0.0f, 1.0f);
        }

        if (colony->cell_count > colony->max_cell_count) {
            colony->max_cell_count = colony->cell_count;
        }
        colony->age++;
        colony->wobble_phase += 0.03f;
        if (colony->wobble_phase > 6.28318f) colony->wobble_phase -= 6.28318f;
        colony->shape_evolution += 0.002f;
        if (colony->shape_evolution > 100.0f) colony->shape_evolution -= 100.0f;
    }

    free(nutrient_sum);
    free(toxin_sum);
    free(own_signal_sum);
    free(alarm_sum);
    free(enemy_pressure);
    free(border_count);
}

void simulation_apply_horizontal_gene_transfer(World* world) {
    if (!world) return;

    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            Cell* cell = world_get_cell(world, x, y);
            if (!cell || cell->colony_id == 0) continue;

            Colony* colony = world_get_colony(world, cell->colony_id);
            if (!colony || !colony->active) continue;

            for (int d = 0; d < 4; d++) {
                Cell* neighbor = world_get_cell(world, x + DX[d], y + DY[d]);
                if (!neighbor || neighbor->colony_id == 0 || neighbor->colony_id == cell->colony_id) {
                    continue;
                }
                if (cell->colony_id > neighbor->colony_id) {
                    continue;
                }

                Colony* other = world_get_colony(world, neighbor->colony_id);
                if (!other || !other->active) continue;

                float transfer_rate = (colony->genome.gene_transfer_rate + other->genome.gene_transfer_rate) * 0.5f;
                transfer_rate *= 0.6f + (colony->behavior_actions[COLONY_ACTION_TRANSFER] +
                                         other->behavior_actions[COLONY_ACTION_TRANSFER]) * 0.35f;
                if (transfer_rate <= 0.0001f) continue;

                float compatibility = 1.0f - utils_clamp_f(genome_distance(&colony->genome, &other->genome), 0.0f, 1.0f);
                float transfer_chance = transfer_rate * (0.08f + compatibility * 0.12f + (colony->stress_level + other->stress_level) * 0.05f);
                if (rand_float() >= transfer_chance) {
                    continue;
                }

                Colony* recipient = colony;
                Colony* donor = other;
                if (other->stress_level > colony->stress_level || other->cell_count < colony->cell_count) {
                    recipient = other;
                    donor = colony;
                }

                float transfer_strength = utils_clamp_f(0.08f + transfer_rate * 1.6f, 0.05f, 0.35f);
                genome_transfer_genes(&recipient->genome, &donor->genome, transfer_strength);
                recipient->stress_level = utils_clamp_f(recipient->stress_level - 0.02f, 0.0f, 1.0f);
                donor->signal_strength = utils_clamp_f(donor->signal_strength + 0.03f, 0.0f, 1.0f);
            }
        }
    }
}

// Decay toxins over time
void simulation_decay_toxins(World* world) {
    if (!world || !world->toxins) return;
    
    int total_cells = world->width * world->height;
    for (int i = 0; i < total_cells; i++) {
        world->toxins[i] = utils_clamp_f(world->toxins[i] - TOXIN_DECAY_RATE, 0.0f, 1.0f);
    }
}

// ============================================================================
// Competitive Strategy Functions
// ============================================================================

// Produce toxins around colony borders
void simulation_produce_toxins(World* world) {
    if (!world || !world->toxins) return;
    
    // Decay existing toxins
    int total_cells = world->width * world->height;
    for (int i = 0; i < total_cells; i++) {
        world->toxins[i] *= 0.95f;  // 5% decay per tick
    }
    
    // Each border cell produces toxins
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            Cell* cell = world_get_cell(world, x, y);
            if (!cell || cell->colony_id == 0 || !cell->is_border) continue;
            
            Colony* colony = world_get_colony(world, cell->colony_id);
            if (!colony || !colony->active) continue;
            
            // Border cells produce toxins based on toxin_production trait
            // High defense_priority increases toxin output at borders
            float production = colony->genome.toxin_production * 
                               (1.0f + colony->genome.defense_priority * 0.5f);
            
            // Emit toxins to neighboring cells
            for (int d = 0; d < 4; d++) {
                int nx = x + DX[d];
                int ny = y + DY[d];
                if (nx < 0 || nx >= world->width || ny < 0 || ny >= world->height) continue;
                
                int idx = ny * world->width + nx;
                world->toxins[idx] = utils_clamp_f(world->toxins[idx] + production * 0.1f, 0.0f, 1.0f);
            }
        }
    }
}

// Apply toxin damage to cells
void simulation_apply_toxin_damage(World* world) {
    if (!world || !world->toxins) return;
    
    // Collect cells that should die from toxins
    typedef struct { int x, y; } DeadCell;
    DeadCell* dead = NULL;
    int dead_count = 0;
    int dead_capacity = 64;
    dead = (DeadCell*)malloc(dead_capacity * sizeof(DeadCell));
    if (!dead) return;
    
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            int idx = y * world->width + x;
            Cell* cell = world_get_cell(world, x, y);
            if (!cell || cell->colony_id == 0) continue;
            
            Colony* colony = world_get_colony(world, cell->colony_id);
            if (!colony || !colony->active) continue;
            
            float toxin_level = world->toxins[idx];
            if (toxin_level <= 0.01f) continue;
            
            // Damage = toxin_level * (1 - resistance) * vulnerability_factor
            // High defense_priority reduces damage at borders
            float vulnerability = cell->is_border ? 
                (1.0f - colony->genome.defense_priority * 0.3f) : 1.0f;
            float damage = toxin_level * (1.0f - colony->genome.toxin_resistance) * vulnerability;
            
            // Probabilistic death based on damage
            if (rand_float() < damage * 0.15f) {
                if (dead_count >= dead_capacity) {
                    dead_capacity *= 2;
                    dead = (DeadCell*)realloc(dead, dead_capacity * sizeof(DeadCell));
                    if (!dead) return;
                }
                dead[dead_count++] = (DeadCell){x, y};
            }
        }
    }
    
    // Kill cells
    for (int i = 0; i < dead_count; i++) {
        Cell* cell = world_get_cell(world, dead[i].x, dead[i].y);
        if (cell && cell->colony_id != 0) {
            Colony* colony = world_get_colony(world, cell->colony_id);
            if (colony && colony->cell_count > 0) {
                colony->cell_count--;
            }
            cell->colony_id = 0;
            cell->age = 0;
            cell->is_border = false;
        }
    }
    
    free(dead);
}

// Handle resource consumption and nutrient depletion
void simulation_consume_resources(World* world) {
    if (!world || !world->nutrients) return;
    
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            int idx = y * world->width + x;
            Cell* cell = world_get_cell(world, x, y);
            if (!cell || cell->colony_id == 0) continue;
            
            Colony* colony = world_get_colony(world, cell->colony_id);
            if (!colony || !colony->active) continue;
            
            // Consume nutrients based on resource_consumption trait
            // High aggression also increases consumption
            float consumption = colony->genome.resource_consumption * 
                                (0.5f + colony->genome.aggression * 0.5f);
            
            // Deplete nutrients more aggressively - creates starvation pressure
            world->nutrients[idx] = utils_clamp_f(
                world->nutrients[idx] - consumption * 0.05f, 0.0f, 1.0f);
        }
    }
    
    // Nutrient regeneration - faster in empty cells, slower in occupied
    for (int i = 0; i < world->width * world->height; i++) {
        if (world->cells[i].colony_id == 0) {
            // Empty cells regenerate quickly
            world->nutrients[i] = utils_clamp_f(world->nutrients[i] + 0.02f, 0.0f, 1.0f);
        } else {
            // Occupied cells regenerate very slowly
            world->nutrients[i] = utils_clamp_f(world->nutrients[i] + 0.002f, 0.0f, 1.0f);
        }
    }
    
    // Toxin decay
    for (int i = 0; i < world->width * world->height; i++) {
        world->toxins[i] = utils_clamp_f(world->toxins[i] - 0.01f, 0.0f, 1.0f);
    }
}

// Combat resolution when colonies meet at borders
void simulation_resolve_combat(World* world) {
    if (!world) return;
    
    // Decay existing toxins
    if (world->toxins) {
        int total = world->width * world->height;
        for (int i = 0; i < total; i++) {
            world->toxins[i] *= 0.95f;  // 5% decay per tick
        }
    }
    
    typedef struct { int x, y; uint32_t winner; uint32_t loser; float strength; } CombatResult;
    CombatResult* results = NULL;
    int result_count = 0;
    int result_capacity = 128;
    results = (CombatResult*)malloc(result_capacity * sizeof(CombatResult));
    if (!results) return;
    
    // First pass: emit toxins from aggressive colonies to create hostile zones
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            Cell* cell = world_get_cell(world, x, y);
            if (!cell || cell->colony_id == 0 || !cell->is_border) continue;
            
            Colony* colony = world_get_colony(world, cell->colony_id);
            if (!colony || !colony->active) continue;
            
            // Border cells emit toxins based on toxin_production
            float toxin_emit = colony->genome.toxin_production * 0.1f;
            if (toxin_emit > 0.01f) {
                // Emit to self and neighbors
                int idx = y * world->width + x;
                world->toxins[idx] = utils_clamp_f(world->toxins[idx] + toxin_emit, 0.0f, 1.0f);
                for (int d = 0; d < 4; d++) {
                    int nx = x + DX[d], ny = y + DY[d];
                    if (nx >= 0 && nx < world->width && ny >= 0 && ny < world->height) {
                        int ni = ny * world->width + nx;
                        world->toxins[ni] = utils_clamp_f(world->toxins[ni] + toxin_emit * 0.5f, 0.0f, 1.0f);
                    }
                }
            }
        }
    }
    
    // Second pass: resolve combat at borders with strategic modifiers
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            Cell* cell = world_get_cell(world, x, y);
            if (!cell || cell->colony_id == 0 || !cell->is_border) continue;
            
            Colony* attacker = world_get_colony(world, cell->colony_id);
            if (!attacker || !attacker->active) continue;

            float attacker_attack_action = attacker->behavior_actions[COLONY_ACTION_ATTACK];
            float attacker_defend_action = attacker->behavior_actions[COLONY_ACTION_DEFEND];
            float attacker_signal_action = attacker->behavior_actions[COLONY_ACTION_SIGNAL];
             
            // Skip if colony is in retreat mode (stressed and defensive)
            if ((attacker->stress_level > 0.7f && attacker->genome.defense_priority > 0.6f) ||
                (attacker_attack_action < 0.35f && attacker_defend_action > attacker_attack_action + 0.15f)) {
                continue; // Defensive colonies under stress don't attack
            }
            
            int attacker_friendly = count_friendly_neighbors(world, x, y, cell->colony_id);
            
            // Check neighbors for enemy cells
            for (int d = 0; d < 4; d++) {
                int nx = x + DX[d];
                int ny = y + DY[d];
                
                Cell* neighbor = world_get_cell(world, nx, ny);
                if (!neighbor || neighbor->colony_id == 0 || 
                    neighbor->colony_id == cell->colony_id) continue;
                
                Colony* defender = world_get_colony(world, neighbor->colony_id);
                if (!defender || !defender->active) continue;
                float defender_defend_action = defender->behavior_actions[COLONY_ACTION_DEFEND];
                float defender_attack_action = defender->behavior_actions[COLONY_ACTION_ATTACK];
                float defender_dormancy_action = defender->behavior_actions[COLONY_ACTION_DORMANCY];
                 
                // === STRATEGIC COMBAT CALCULATION ===
                
                // Base strength from genome
                float attack_str = attacker->genome.aggression * 1.2f;
                float defend_str = defender->genome.resilience * 1.0f;
                
                // 1. FLANKING BONUS: More friendly neighbors = stronger attack
                float flanking_bonus = 1.0f + (attacker_friendly * 0.15f);
                attack_str *= flanking_bonus;
                
                // 2. DEFENSIVE FORMATION: Defenders with high defense_priority are harder to crack
                int defender_friendly = count_friendly_neighbors(world, nx, ny, neighbor->colony_id);
                float defensive_bonus = 1.0f + (defender->genome.defense_priority * defender_friendly * 0.2f);
                defend_str *= defensive_bonus;
                
                // 3. DIRECTIONAL PREFERENCE: Colonies fight harder in preferred directions
                float dir_weight = get_direction_weight(&attacker->genome, DX[d], DY[d]);
                attack_str *= (0.7f + dir_weight * 0.6f); // 0.7-1.3x based on preferred direction
                
                // 4. TOXIN WARFARE: Toxin production aids attack, resistance aids defense
                attack_str += attacker->genome.toxin_production * 0.4f;
                defend_str += defender->genome.toxin_resistance * 0.3f;
                
                // 5. BIOFILM DEFENSE: Defenders with biofilm are harder to defeat
                defend_str *= (1.0f + defender->biofilm_strength * 0.3f);
                
                // 6. NUTRIENT ADVANTAGE: Well-fed cells fight better
                if (world->nutrients) {
                    int attacker_idx = y * world->width + x;
                    int defender_idx = ny * world->width + nx;
                    attack_str *= (0.6f + world->nutrients[attacker_idx] * 0.5f);
                    defend_str *= (0.6f + world->nutrients[defender_idx] * 0.5f);
                    
                    // Toxin damage reduces effectiveness
                    attack_str *= (1.0f - world->toxins[attacker_idx] * (1.0f - attacker->genome.toxin_resistance));
                    defend_str *= (1.0f - world->toxins[defender_idx] * (1.0f - defender->genome.toxin_resistance));
                }

                // 7. BEHAVIOR GRAPH: current action mix changes border combat posture
                attack_str *= (0.75f + attacker_attack_action * 0.85f);
                attack_str *= (0.90f + attacker->behavior_actions[COLONY_ACTION_EXPAND] * 0.25f);
                attack_str *= (1.0f - attacker_signal_action * 0.10f);
                defend_str *= (0.75f + defender_defend_action * 0.90f);
                defend_str *= (0.95f + defender_dormancy_action * 0.15f);

                if (attacker->behavior_mode == COLONY_BEHAVIOR_MODE_RAIDING) {
                    attack_str *= 1.10f;
                } else if (attacker->behavior_mode == COLONY_BEHAVIOR_MODE_COOPERATING ||
                           attacker->behavior_mode == COLONY_BEHAVIOR_MODE_SURVIVAL) {
                    attack_str *= 0.88f;
                }

                if (defender->behavior_mode == COLONY_BEHAVIOR_MODE_FORTIFYING ||
                    defender->behavior_mode == COLONY_BEHAVIOR_MODE_SURVIVAL ||
                    defender->behavior_mode == COLONY_BEHAVIOR_MODE_DORMANT) {
                    defend_str *= 1.12f;
                }

                // 8. MOMENTUM: Colonies that have been winning keep winning (success_history)
                attack_str *= (0.8f + attacker->success_history[d] * 0.4f);
                 
                // 9. STRESSED COLONIES FIGHT DIFFERENTLY
                if (attacker->stress_level > 0.5f) {
                    // Desperate attacks: higher risk, higher reward
                    attack_str *= (1.0f + attacker->genome.aggression * 0.3f);
                }
                if (defender->stress_level > 0.5f && defender->genome.defense_priority < 0.4f &&
                    defender_defend_action < defender_attack_action) {
                    // Stressed non-defensive colonies crumble
                    defend_str *= 0.7f;
                }
                 
                // 10. SIZE MATTERS: Larger colonies have morale advantage
                float size_ratio = (float)attacker->cell_count / (float)(defender->cell_count + 1);
                if (size_ratio > 2.0f) attack_str *= 1.15f;  // 2x larger = bonus
                if (size_ratio < 0.5f) attack_str *= 0.85f;  // 2x smaller = penalty
                
                // === COMBAT RESOLUTION ===
                float attack_chance = attack_str / (attack_str + defend_str + 0.1f);
                
                // High combat rate for dynamic, active borders
                if (rand_float() < attack_chance * 0.9f) {
                    // Attacker wins - record result
                    if (result_count >= result_capacity) {
                        result_capacity *= 2;
                        CombatResult* new_results = (CombatResult*)realloc(results, result_capacity * sizeof(CombatResult));
                        if (!new_results) { free(results); return; }
                        results = new_results;
                    }
                    results[result_count++] = (CombatResult){nx, ny, cell->colony_id, neighbor->colony_id, attack_str};
                    
                    // Update success history for learning
                    if (d < 8) {
                        attacker->success_history[d] = utils_clamp_f(
                            attacker->success_history[d] + 0.05f * attacker->genome.learning_rate, 0.0f, 1.0f);
                    }
                } else if (rand_float() < 0.3f) {
                    // Defender successfully defends - slight penalty to attacker's direction
                    if (d < 8) {
                        attacker->success_history[d] = utils_clamp_f(
                            attacker->success_history[d] - 0.02f * attacker->genome.learning_rate, 0.0f, 1.0f);
                    }
                }
            }
        }
    }
    
    // Apply combat results
    for (int i = 0; i < result_count; i++) {
        Cell* cell = world_get_cell(world, results[i].x, results[i].y);
        if (cell && cell->colony_id == results[i].loser) {
            Colony* loser = world_get_colony(world, results[i].loser);
            Colony* winner = world_get_colony(world, results[i].winner);
            
            if (loser && loser->cell_count > 0) {
                loser->cell_count--;
                // Increase stress when losing cells
                loser->stress_level = utils_clamp_f(loser->stress_level + 0.01f, 0.0f, 1.0f);
            }
            
            // Cell is captured by winner
            if (winner && winner->active) {
                cell->colony_id = results[i].winner;
                cell->age = 0;
                cell->is_border = true;
                winner->cell_count++;
                // Reduce stress when winning
                winner->stress_level = utils_clamp_f(winner->stress_level - 0.005f, 0.0f, 1.0f);
            } else {
                cell->colony_id = 0;
                cell->age = 0;
                cell->is_border = false;
            }
        }
    }
    
    free(results);
}

void simulation_tick(World* world) {
    if (!world) return;
    
    // Age all cells and handle starvation/toxin death
    for (int i = 0; i < world->width * world->height; i++) {
        Cell* cell = &world->cells[i];
        if (cell->colony_id == 0) continue;
        
        // Age the cell
        if (cell->age < 255) cell->age++;
        
        Colony* colony = world_get_colony(world, cell->colony_id);
        if (!colony || !colony->active) continue;
        
        // STARVATION: Cells in depleted areas may die
        float nutrients = world->nutrients[i];
        if (nutrients < 0.2f) {
            // Low nutrients - chance of cell death based on efficiency
            float death_chance = (0.2f - nutrients) * 0.1f * (1.0f - colony->genome.efficiency);
            if (rand_float() < death_chance) {
                cell->colony_id = 0;
                cell->age = 0;
                cell->is_border = false;
                if (colony->cell_count > 0) colony->cell_count--;
                colony->stress_level = utils_clamp_f(colony->stress_level + 0.02f, 0.0f, 1.0f);
                continue;
            }
        }
        
        // TOXIN DEATH: Cells in toxic areas may die
        float toxins = world->toxins[i];
        if (toxins > 0.3f) {
            float death_chance = (toxins - 0.3f) * 0.15f * (1.0f - colony->genome.toxin_resistance);
            if (rand_float() < death_chance) {
                cell->colony_id = 0;
                cell->age = 0;
                cell->is_border = false;
                if (colony->cell_count > 0) colony->cell_count--;
                colony->stress_level = utils_clamp_f(colony->stress_level + 0.02f, 0.0f, 1.0f);
                continue;
            }
        }
        
        // NATURAL DECAY: All cells have a small baseline death rate
        // This ensures colonies shrink over time if they're not actively growing
        // Border cells die faster (exposed), interior cells are more stable
        float base_death_rate = 0.005f;  // ~0.5% per tick baseline (increased for more churn)
        if (cell->is_border) {
            base_death_rate = 0.012f;  // Border cells more vulnerable
        }
        // Biofilm protects against natural decay
        base_death_rate *= (1.0f - colony->biofilm_strength * 0.5f);
        // Efficiency reduces decay
        base_death_rate *= (1.0f - colony->genome.efficiency * 0.3f);
        
        if (rand_float() < base_death_rate) {
            cell->colony_id = 0;
            cell->age = 0;
            cell->is_border = false;
            if (colony->cell_count > 0) colony->cell_count--;
            continue;
        }
        
        // OLD AGE: Very old cells have higher chance of dying
        if (cell->age > 150) {
            float age_death_chance = (cell->age - 150) * 0.0003f;  // Gradual increase
            if (rand_float() < age_death_chance) {
                cell->colony_id = 0;
                cell->age = 0;
                cell->is_border = false;
                if (colony->cell_count > 0) colony->cell_count--;
            }
        }
    }
    
    // Update environmental layers and colony signaling before spread
    simulation_update_behavior_layers(world);
    
    // ENVIRONMENTAL DISTURBANCES: Periodic events to prevent stagnation
    // Every ~100 ticks, create a nutrient-depleted zone
    if (world->tick % 100 == 0 && rand_float() < 0.5f) {
        int cx = rand() % world->width;
        int cy = rand() % world->height;
        int radius = 5 + rand() % 15;  // Radius 5-20
        for (int y = cy - radius; y <= cy + radius; y++) {
            for (int x = cx - radius; x <= cx + radius; x++) {
                if (x >= 0 && x < world->width && y >= 0 && y < world->height) {
                    int dist2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
                    if (dist2 <= radius * radius) {
                        int idx = y * world->width + x;
                        world->nutrients[idx] *= 0.3f;  // Deplete nutrients in zone
                    }
                }
            }
        }
    }
    
    // Run simulation phases
    simulation_spread(world);
    simulation_mutate(world);
    simulation_check_divisions(world);
    simulation_check_recombinations(world);
    
    // Combat resolution for more dynamic battles
    simulation_resolve_combat(world);

    // Contact-based adaptation between neighboring colonies
    simulation_apply_horizontal_gene_transfer(world);
    
    // Count active colonies and total cells
    int active_colonies = 0;
    int total_cells = 0;
    for (size_t i = 0; i < world->colony_count; i++) {
        if (world->colonies[i].active) {
            active_colonies++;
            total_cells += world->colonies[i].cell_count;
        }
    }
    
    // Spontaneous generation: spawn new colonies in empty areas
    // More likely when there's lots of empty space
    int world_size = world->width * world->height;
    float empty_ratio = 1.0f - (float)total_cells / (float)world_size;
    if (empty_ratio > 0.3f && active_colonies < 100 && rand_float() < empty_ratio * 0.02f) {
        // Find a random empty spot
        for (int attempts = 0; attempts < 20; attempts++) {
            int x = rand() % world->width;
            int y = rand() % world->height;
            Cell* cell = world_get_cell(world, x, y);
            if (cell && cell->colony_id == 0) {
                // Spawn a new random colony here
                Colony colony;
                memset(&colony, 0, sizeof(Colony));
                colony.genome = genome_create_random();
                colony.color = colony.genome.body_color;
                colony.cell_count = 1;
                colony.max_cell_count = 1;
                colony.active = true;
                colony.parent_id = 0;
                colony.shape_seed = (uint32_t)rand() ^ ((uint32_t)rand() << 16);
                colony.wobble_phase = (float)(rand() % 628) / 100.0f;
                generate_scientific_name(colony.name, sizeof(colony.name));
                
                uint32_t id = world_add_colony(world, colony);
                if (id > 0) {
                    cell->colony_id = id;
                    cell->age = 0;
                }
                break;
            }
        }
    }
    
    // Recount all colony cell counts from grid to fix any inconsistencies
    simulation_update_colony_stats(world);

    // Update colony lifecycle, learning, and movement dynamics
    simulation_update_colony_dynamics(world);
    
    world->tick++;
}
