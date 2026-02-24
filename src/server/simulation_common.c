#include "simulation_common.h"
#include "world.h"
#include "genetics.h"
#include "../shared/utils.h"
#include <math.h>

const int DX8[] = {0, 1, 1, 1, 0, -1, -1, -1};
const int DY8[] = {-1, -1, 0, 1, 1, 1, 0, -1};
const float DIR8_WEIGHT[] = {1.0f, 0.7071f, 1.0f, 0.7071f, 1.0f, 0.7071f, 1.0f, 0.7071f};

float calculate_local_density(World* world, int x, int y, uint32_t colony_id) {
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

float calculate_env_spread_modifier(World* world, Colony* colony, int tx, int ty, int sx, int sy) {
    int target_idx = ty * world->width + tx;
    float modifier = 1.0f;
    
    float nutrient = world->nutrients[target_idx];
    modifier *= (1.0f + colony->genome.nutrient_sensitivity * (nutrient - 0.5f));
    
    float toxin = world->toxins[target_idx];
    modifier *= (1.0f - colony->genome.toxin_sensitivity * toxin);
    
    float edge_dist_x = fminf((float)tx, (float)(world->width - 1 - tx)) / (float)(world->width / 2);
    float edge_dist_y = fminf((float)ty, (float)(world->height - 1 - ty)) / (float)(world->height / 2);
    float edge_factor = 1.0f - fminf(edge_dist_x, edge_dist_y);
    modifier *= (1.0f + colony->genome.edge_affinity * (edge_factor - 0.5f));
    
    float local_density = calculate_local_density(world, sx, sy, colony->id);
    if (local_density > colony->genome.quorum_threshold) {
        float density_penalty = (local_density - colony->genome.quorum_threshold) * 
                                (1.0f - colony->genome.density_tolerance);
        modifier *= (1.0f - density_penalty);
    }
    
    return utils_clamp_f(modifier, 0.3f, 2.0f);
}

float calculate_biomass_pressure(World* world, int x, int y, uint32_t colony_id) {
    int same_count = 0;
    int total = 0;
    
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            Cell* n = world_get_cell(world, x + dx, y + dy);
            if (n) {
                total++;
                if (n->colony_id == colony_id) same_count++;
            }
        }
    }
    
    if (total == 0) return 1.0f;
    
    float density = (float)same_count / (float)total;
    return 1.0f + density * 0.5f;
}

float get_quorum_activation(const Colony* colony) {
    if (!colony) return 0.0f;
    float threshold = colony->genome.quorum_threshold;
    if (colony->signal_strength <= threshold) return 0.0f;
    return utils_clamp_f(
        (colony->signal_strength - threshold) / (1.0f - threshold + 0.001f),
        0.0f, 1.0f
    );
}

float get_direction_weight(Genome* g, int dx, int dy) {
    if (dy == -1 && dx == 0) return g->spread_weights[0];
    if (dy == -1 && dx == 1) return g->spread_weights[1];
    if (dy == 0  && dx == 1) return g->spread_weights[2];
    if (dy == 1  && dx == 1) return g->spread_weights[3];
    if (dy == 1  && dx == 0) return g->spread_weights[4];
    if (dy == 1  && dx ==-1) return g->spread_weights[5];
    if (dy == 0  && dx ==-1) return g->spread_weights[6];
    if (dy == -1 && dx ==-1) return g->spread_weights[7];
    return 1.0f;
}

float calculate_curvature_boost(World* world, int tx, int ty, uint32_t colony_id) {
    int same = 0;
    for (int d = 0; d < 8; d++) {
        int nx = tx + DX8[d], ny = ty + DY8[d];
        if (nx >= 0 && nx < world->width && ny >= 0 && ny < world->height) {
            if (world->cells[ny * world->width + nx].colony_id == colony_id) same++;
        }
    }
    return 0.85f + (float)same * 0.15f;
}

float calculate_perception_modifier(World* world, int x, int y,
                                    int dx, int dy, Colony* colony) {
    float range = colony->genome.detection_range;
    if (range < 0.05f) return 1.0f;

    int scan_dist = 2 + (int)(range * 6.0f);

    float nutrient_sum = 0.0f;
    float empty_count = 0.0f;
    float enemy_count = 0.0f;
    int samples = 0;

    for (int step = 1; step <= scan_dist; step++) {
        int sx = x + dx * step;
        int sy = y + dy * step;
        if (sx < 0 || sx >= world->width || sy < 0 || sy >= world->height) break;
        int idx = sy * world->width + sx;
        nutrient_sum += world->nutrients[idx];
        if (world->cells[idx].colony_id == 0)
            empty_count += 1.0f;
        else if (world->cells[idx].colony_id != colony->id)
            enemy_count += 1.0f;
        samples++;
    }

    if (samples == 0) return 1.0f;

    float inv = 1.0f / samples;
    float nutrient_score = nutrient_sum * inv;
    float space_score = empty_count * inv;
    float threat_score = enemy_count * inv;

    float nutrient_boost = 1.0f + colony->genome.nutrient_sensitivity * nutrient_score * 0.5f;
    float space_boost = 1.0f + space_score * 0.3f;

    float threat_mod = 1.0f;
    if (threat_score > 0.0f) {
        if (colony->genome.aggression > 0.5f) {
            threat_mod = 1.0f + (colony->genome.aggression - 0.5f) * threat_score * 0.6f;
        } else {
            threat_mod = 1.0f - (0.5f - colony->genome.aggression) * threat_score * 0.4f;
        }
    }

    float result = nutrient_boost * space_boost * threat_mod;
    if (result < 0.5f) result = 0.5f;
    if (result > 2.0f) result = 2.0f;
    return result;
}

float get_scent_influence(World* world, int x, int y, int dx, int dy, 
                          uint32_t colony_id, const Genome* genome) {
    int nx = x + dx, ny = y + dy;
    if (nx < 0 || nx >= world->width || ny < 0 || ny >= world->height) {
        return 1.0f;
    }
    
    int idx = ny * world->width + nx;
    float scent = world->signals[idx];
    uint32_t source = world->signal_source[idx];
    
    if (scent < 0.01f || source == 0) return 1.0f;
    
    if (source == colony_id) {
        return 1.0f - scent * (1.0f - genome->density_tolerance) * 0.3f;
    }
    
    float aggression = genome->aggression;
    float defense = genome->defense_priority;
    
    float reaction = (aggression - defense) * genome->signal_sensitivity;
    
    return 1.0f + reaction * scent * 0.5f;
}

int count_friendly_neighbors(World* world, int x, int y, uint32_t colony_id) {
    int count = 0;
    for (int d = 0; d < 8; d++) {
        Cell* n = world_get_cell(world, x + DX8[d], y + DY8[d]);
        if (n && n->colony_id == colony_id) count++;
    }
    return count;
}

int count_enemy_neighbors(World* world, int x, int y, uint32_t colony_id) {
    int count = 0;
    for (int d = 0; d < 8; d++) {
        Cell* n = world_get_cell(world, x + DX8[d], y + DY8[d]);
        if (n && n->colony_id != 0 && n->colony_id != colony_id) count++;
    }
    return count;
}

float calculate_expensive_trait_load(const Genome* genome) {
    if (!genome) return 0.0f;

    // Energetic burden from expensive social traits.
    // Weights are calibrated so all-max does not collapse the simulation,
    // but is still clearly penalized versus balanced configurations.
    const float toxin_cost = genome->toxin_production * 0.24f;
    const float biofilm_cost = (genome->biofilm_investment * genome->biofilm_tendency) * 0.20f;
    const float signaling_cost = genome->signal_emission * 0.16f;
    const float motility_cost = genome->motility * 0.18f;

    return utils_clamp_f(toxin_cost + biofilm_cost + signaling_cost + motility_cost, 0.0f, 1.0f);
}

float calculate_growth_cost_multiplier(const Colony* colony) {
    if (!colony) return 1.0f;

    float load = calculate_expensive_trait_load(&colony->genome);
    return utils_clamp_f(1.0f - load, 0.35f, 1.0f);
}

float calculate_survival_cost_multiplier(const Colony* colony) {
    if (!colony) return 1.0f;

    float load = calculate_expensive_trait_load(&colony->genome);
    return 1.0f + load * 0.55f;
}

bool colony_in_persister_state(const Colony* colony) {
    if (!colony) return false;
    return colony->is_dormant || colony->is_persister;
}

void colony_update_persister_switching(Colony* colony) {
    if (!colony) return;

    if (colony->is_dormant) {
        colony->is_persister = true;
        return;
    }

    float enter_stress = utils_clamp_f(colony->genome.persister_entry_stress, 0.15f, 0.98f);
    float exit_stress = utils_clamp_f(colony->genome.persister_exit_stress, 0.0f, 0.92f);
    if (exit_stress >= enter_stress - 0.02f) {
        exit_stress = utils_clamp_f(enter_stress - 0.02f, 0.0f, 0.92f);
    }

    float entry_rate = utils_clamp_f(colony->genome.persister_entry_rate, 0.0f, 1.0f);
    float exit_rate = utils_clamp_f(colony->genome.persister_exit_rate, 0.0f, 1.0f);

    if (!colony->is_persister) {
        if (colony->stress_level > enter_stress) {
            float stress_pressure = (colony->stress_level - enter_stress) / (1.0f - enter_stress + 0.001f);
            float chance = entry_rate * (0.25f + stress_pressure * 0.75f);
            chance = utils_clamp_f(chance, 0.0f, 1.0f);
            if (rand_float() < chance) {
                colony->is_persister = true;
            }
        }
    } else if (colony->stress_level < exit_stress) {
        float relief = (exit_stress - colony->stress_level) / (exit_stress + 0.001f);
        float chance = exit_rate * (0.20f + relief * 0.80f);
        chance *= (1.0f - colony->genome.dormancy_resistance * 0.30f);
        chance = utils_clamp_f(chance, 0.0f, 1.0f);
        if (rand_float() < chance) {
            colony->is_persister = false;
        }
    }
}

float colony_spread_activity_factor(const Colony* colony) {
    if (!colony) return 1.0f;
    if (colony->is_dormant) {
        return 0.12f + colony->genome.dormancy_resistance * 0.28f;
    }
    if (colony->is_persister) {
        return 0.40f + colony->genome.dormancy_resistance * 0.25f;
    }
    return 1.0f;
}

float colony_signal_activity_factor(const Colony* colony) {
    if (!colony) return 1.0f;
    if (colony->is_dormant) return 0.60f;
    if (colony->is_persister) return 0.78f;
    return 1.0f;
}

float colony_toxin_output_factor(const Colony* colony) {
    if (!colony) return 1.0f;
    if (colony->is_dormant) return 0.50f;
    if (colony->is_persister) return 0.72f;
    return 1.0f;
}

float colony_turnover_factor(const Colony* colony) {
    if (!colony) return 1.0f;
    if (colony->is_dormant) {
        float protection = 0.50f - colony->genome.dormancy_resistance * 0.35f;
        return utils_clamp_f(protection, 0.12f, 0.50f);
    }
    if (colony->is_persister) {
        float protection = 0.88f - colony->genome.dormancy_resistance * 0.18f;
        return utils_clamp_f(protection, 0.62f, 0.88f);
    }
    return 1.0f;
}
