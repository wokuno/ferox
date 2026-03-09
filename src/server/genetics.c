#include "genetics.h"
#include "../shared/utils.h"
#include <stdlib.h>
#include <math.h>

#define MUTATION_DELTA 0.1f
#define GENOME_DISTANCE_WEIGHT_SUM 28.25f
#define LEGACY_DISTANCE_WEIGHT 0.55f
#define GRAPH_DISTANCE_WEIGHT 0.45f

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float clamp_signed(float value) {
    return utils_clamp_f(value, -1.0f, 1.0f);
}

static float trait_to_bias(float value) {
    return clamp_signed(value * 2.0f - 1.0f);
}

static float positive_edge_affinity(float edge_affinity) {
    return utils_clamp_f(0.5f + edge_affinity * 0.5f, 0.0f, 1.0f);
}

static float average_abs_signed_diff(const float* a, const float* b, size_t count) {
    if (!a || !b || count == 0) return 0.0f;

    float total = 0.0f;
    for (size_t i = 0; i < count; i++) {
        total += utils_abs_f(a[i] - b[i]) * 0.5f;
    }
    return total / (float)count;
}

static float average_abs_unit_diff(const float* a, const float* b, size_t count) {
    if (!a || !b || count == 0) return 0.0f;

    float total = 0.0f;
    for (size_t i = 0; i < count; i++) {
        total += utils_abs_f(a[i] - b[i]);
    }
    return total / (float)count;
}

static void genome_seed_behavior_graph(Genome* g) {
    static const float base_drive_weights[COLONY_DRIVE_COUNT][COLONY_SENSOR_COUNT] = {
        { 0.70f, -0.45f,  0.10f, -0.20f,  0.55f, -0.20f,  0.25f,  0.55f },
        {-0.10f,  0.55f,  0.10f,  0.60f, -0.10f,  0.55f, -0.10f, -0.15f },
        { 0.10f,  0.20f, -0.20f,  0.30f,  0.35f,  0.70f,  0.20f,  0.15f },
        { 0.20f, -0.15f,  0.75f,  0.20f,  0.15f, -0.20f,  0.10f,  0.20f },
        { 0.35f, -0.20f,  0.10f, -0.20f,  0.70f,  0.10f,  0.35f,  0.55f },
        {-0.15f,  0.45f,  0.20f,  0.55f, -0.10f,  0.45f, -0.15f, -0.20f },
    };
    static const float base_action_weights[COLONY_ACTION_COUNT][COLONY_DRIVE_COUNT] = {
        { 0.75f, -0.35f,  0.10f,  0.05f,  0.55f, -0.25f },
        { 0.20f, -0.15f,  0.85f, -0.25f,  0.15f, -0.10f },
        {-0.05f,  0.75f,  0.20f,  0.05f, -0.10f,  0.55f },
        { 0.10f,  0.20f, -0.10f,  0.80f,  0.10f,  0.20f },
        { 0.05f,  0.10f, -0.20f,  0.60f,  0.10f,  0.35f },
        {-0.45f,  0.35f, -0.10f, -0.05f, -0.20f,  0.90f },
        { 0.20f, -0.15f,  0.05f,  0.05f,  0.80f, -0.20f },
    };

    g->behavior_sensor_gains[COLONY_SENSOR_NUTRIENT] = utils_clamp_f(0.35f + g->nutrient_sensitivity * 0.65f, 0.0f, 1.0f);
    g->behavior_sensor_gains[COLONY_SENSOR_TOXIN] = utils_clamp_f(0.25f + g->toxin_sensitivity * 0.75f, 0.0f, 1.0f);
    g->behavior_sensor_gains[COLONY_SENSOR_SIGNAL] = utils_clamp_f(0.25f + g->signal_sensitivity * 0.75f, 0.0f, 1.0f);
    g->behavior_sensor_gains[COLONY_SENSOR_ALARM] = utils_clamp_f(0.25f + g->alarm_threshold * 0.75f, 0.0f, 1.0f);
    g->behavior_sensor_gains[COLONY_SENSOR_FRONTIER] = utils_clamp_f(0.30f + g->spread_rate * 0.70f, 0.0f, 1.0f);
    g->behavior_sensor_gains[COLONY_SENSOR_PRESSURE] = utils_clamp_f(0.30f + fmaxf(g->aggression, g->defense_priority) * 0.70f, 0.0f, 1.0f);
    g->behavior_sensor_gains[COLONY_SENSOR_MOMENTUM] = utils_clamp_f(0.25f + g->memory_factor * 0.75f, 0.0f, 1.0f);
    g->behavior_sensor_gains[COLONY_SENSOR_GROWTH] = utils_clamp_f(0.30f + g->metabolism * 0.70f, 0.0f, 1.0f);

    g->behavior_drive_biases[COLONY_DRIVE_GROWTH] = trait_to_bias((g->spread_rate + g->metabolism + g->nutrient_sensitivity) / 3.0f) + g->hidden_weights[0] * 0.15f;
    g->behavior_drive_biases[COLONY_DRIVE_CAUTION] = trait_to_bias((g->defense_priority + g->toxin_sensitivity + g->alarm_threshold) / 3.0f) + g->hidden_weights[1] * 0.15f;
    g->behavior_drive_biases[COLONY_DRIVE_HOSTILITY] = trait_to_bias((g->aggression + g->toxin_production + (1.0f - g->merge_affinity)) / 3.0f) + g->hidden_weights[2] * 0.15f;
    g->behavior_drive_biases[COLONY_DRIVE_COHESION] = trait_to_bias((g->merge_affinity + g->signal_emission + g->signal_sensitivity) / 3.0f) + g->hidden_weights[3] * 0.15f;
    g->behavior_drive_biases[COLONY_DRIVE_EXPLORATION] = trait_to_bias((g->motility + positive_edge_affinity(g->edge_affinity) + g->nutrient_sensitivity) / 3.0f) + g->hidden_weights[4] * 0.15f;
    g->behavior_drive_biases[COLONY_DRIVE_PRESERVATION] = trait_to_bias((g->dormancy_resistance + g->biofilm_investment + g->resilience) / 3.0f) + g->hidden_weights[5] * 0.15f;

    float drive_scale[COLONY_DRIVE_COUNT] = {
        0.70f + g->spread_rate * 0.35f + g->metabolism * 0.15f,
        0.70f + g->defense_priority * 0.30f + g->toxin_sensitivity * 0.20f,
        0.65f + g->aggression * 0.40f + g->toxin_production * 0.15f,
        0.65f + g->merge_affinity * 0.25f + g->signal_sensitivity * 0.20f,
        0.65f + g->motility * 0.30f + positive_edge_affinity(g->edge_affinity) * 0.15f,
        0.70f + g->dormancy_resistance * 0.25f + g->biofilm_tendency * 0.20f,
    };

    for (int drive = 0; drive < COLONY_DRIVE_COUNT; drive++) {
        for (int sensor = 0; sensor < COLONY_SENSOR_COUNT; sensor++) {
            g->behavior_drive_weights[drive][sensor] = clamp_signed(base_drive_weights[drive][sensor] * drive_scale[drive]);
        }
    }

    g->behavior_action_biases[COLONY_ACTION_EXPAND] = trait_to_bias((g->spread_rate + g->metabolism) * 0.5f) + g->hidden_weights[6] * 0.12f;
    g->behavior_action_biases[COLONY_ACTION_ATTACK] = trait_to_bias((g->aggression + g->toxin_production) * 0.5f);
    g->behavior_action_biases[COLONY_ACTION_DEFEND] = trait_to_bias((g->defense_priority + g->resilience) * 0.5f);
    g->behavior_action_biases[COLONY_ACTION_SIGNAL] = trait_to_bias((g->signal_emission + g->signal_sensitivity) * 0.5f) + g->hidden_weights[7] * 0.12f;
    g->behavior_action_biases[COLONY_ACTION_TRANSFER] = trait_to_bias((g->merge_affinity + utils_clamp_f(g->gene_transfer_rate * 10.0f, 0.0f, 1.0f)) * 0.5f);
    g->behavior_action_biases[COLONY_ACTION_DORMANCY] = trait_to_bias((g->dormancy_threshold + g->dormancy_resistance) * 0.5f);
    g->behavior_action_biases[COLONY_ACTION_MOTILITY] = trait_to_bias((g->motility + positive_edge_affinity(g->edge_affinity)) * 0.5f);

    float action_scale[COLONY_ACTION_COUNT] = {
        0.75f + g->spread_rate * 0.30f,
        0.70f + g->aggression * 0.35f,
        0.70f + g->defense_priority * 0.35f,
        0.70f + g->signal_emission * 0.25f,
        0.65f + g->merge_affinity * 0.20f + utils_clamp_f(g->gene_transfer_rate * 10.0f, 0.0f, 1.0f) * 0.15f,
        0.70f + g->dormancy_resistance * 0.25f,
        0.70f + g->motility * 0.35f,
    };

    for (int action = 0; action < COLONY_ACTION_COUNT; action++) {
        for (int drive = 0; drive < COLONY_DRIVE_COUNT; drive++) {
            g->behavior_action_weights[action][drive] = clamp_signed(base_action_weights[action][drive] * action_scale[action]);
        }
    }
}

Genome genome_create_random(void) {
    Genome g;
    
    // === Basic Traits ===
    for (int i = 0; i < 8; i++) {
        g.spread_weights[i] = 0.3f + rand_float() * 0.7f;
    }
    g.spread_rate = 0.3f + rand_float() * 0.5f;  // 0.3-0.8: faster spread for more activity
    g.mutation_rate = rand_float() * 0.1f;  // 0-0.1
    g.aggression = 0.3f + rand_float() * 0.7f;  // 0.3-1.0: varied aggression
    g.resilience = 0.2f + rand_float() * 0.6f;  // 0.2-0.8
    g.metabolism = 0.4f + rand_float() * 0.5f;  // 0.4-0.9: higher metabolism = faster growth
    
    // === Social Behavior ===
    g.detection_range = 0.1f + rand_float() * 0.4f;  // 0.1-0.5
    g.max_tracked = (uint8_t)(1 + rand_range(0, 3));  // 1-4
    g.social_factor = (rand_float() - 0.5f) * 2.0f;  // -1 to 1
    g.merge_affinity = rand_float() * 0.3f;  // 0-0.3
    
    // === Environmental Sensing ===
    g.nutrient_sensitivity = rand_float() * 0.8f;  // 0-0.8: chemotaxis strength
    g.toxin_sensitivity = 0.3f + rand_float() * 0.6f;  // 0.3-0.9: toxin avoidance
    g.edge_affinity = (rand_float() - 0.5f) * 1.0f;  // -0.5 to 0.5
    g.density_tolerance = 0.3f + rand_float() * 0.7f;  // 0.3-1.0
    g.quorum_threshold = 0.3f + rand_float() * 0.5f;  // 0.3-0.8: quorum sensing threshold
    
    // === Colony Interactions ===
    g.toxin_production = rand_float() * 0.5f;  // 0-0.5: moderate toxin production
    g.toxin_resistance = rand_float() * 0.8f;  // 0-0.8
    g.signal_emission = rand_float() * 0.6f;  // 0-0.6
    g.signal_sensitivity = rand_float() * 0.8f;  // 0-0.8
    g.alarm_threshold = 0.3f + rand_float() * 0.5f;  // 0.3-0.8: sensitivity to hostile contact
    g.gene_transfer_rate = rand_float() * 0.05f;  // 0-0.05: rare gene transfer
    
    // === Competitive Strategy ===
    g.resource_consumption = 0.3f + rand_float() * 0.5f;  // 0.3-0.8: balanced resource use
    g.defense_priority = rand_float() * 0.7f;  // 0-0.7: most colonies moderately defensive
    
    // === Survival Strategies ===
    g.dormancy_threshold = rand_float() * 0.3f;  // 0-0.3: triggers at low population
    g.dormancy_resistance = 0.3f + rand_float() * 0.6f;  // 0.3-0.9
    g.sporulation_threshold = 0.4f + rand_float() * 0.4f;  // 0.4-0.8: stress level for dormancy
    g.biofilm_investment = rand_float() * 0.5f;  // 0-0.5: trade growth for resilience
    g.biofilm_tendency = rand_float() * 0.6f;  // 0-0.6: tendency to form biofilm
    g.motility = rand_float() * 0.3f;  // 0-0.3: colony drift speed
    g.motility_direction = rand_float() * 2.0f * (float)M_PI;  // random direction
    g.specialization = rand_float() * 0.5f;  // 0-0.5: edge vs interior differentiation
    
    // === Metabolic Strategy ===
    g.efficiency = 0.3f + rand_float() * 0.6f;  // 0.3-0.9
    
    // === Neural Network Decision Layer ===
    for (int i = 0; i < 8; i++) {
        g.hidden_weights[i] = (rand_float() - 0.5f) * 2.0f;  // -1 to 1
    }
    g.learning_rate = 0.05f + rand_float() * 0.2f;  // 0.05-0.25
    g.memory_factor = 0.3f + rand_float() * 0.5f;   // 0.3-0.8

    genome_seed_behavior_graph(&g);
    
    // === Colors ===
    g.body_color.r = (uint8_t)rand_range(50, 255);
    g.body_color.g = (uint8_t)rand_range(50, 255);
    g.body_color.b = (uint8_t)rand_range(50, 255);
    g.border_color.r = (uint8_t)(g.body_color.r / 2);
    g.border_color.g = (uint8_t)(g.body_color.g / 2);
    g.border_color.b = (uint8_t)(g.body_color.b / 2);
    
    return g;
}

// Helper macro for mutation
#define MUTATE_FIELD(field, min_val, max_val) \
    if (rand_float() < mutation_chance) { \
        float delta = (rand_float() - 0.5f) * 2.0f * MUTATION_DELTA; \
        genome->field = utils_clamp_f(genome->field + delta, min_val, max_val); \
    }

#define MUTATE_FIELD_SLOW(field, min_val, max_val) \
    if (rand_float() < mutation_chance * 0.5f) { \
        float delta = (rand_float() - 0.5f) * MUTATION_DELTA; \
        genome->field = utils_clamp_f(genome->field + delta, min_val, max_val); \
    }

void genome_mutate(Genome* genome) {
    if (!genome) return;
    
    float mutation_chance = genome->mutation_rate;
    
    // === Basic Traits ===
    MUTATE_FIELD(spread_rate, 0.0f, 1.0f);
    MUTATE_FIELD(mutation_rate, 0.0f, 0.2f);
    MUTATE_FIELD(aggression, 0.0f, 1.0f);
    MUTATE_FIELD(resilience, 0.0f, 1.0f);
    MUTATE_FIELD(metabolism, 0.0f, 1.0f);
    
    // === Social Behavior ===
    MUTATE_FIELD(detection_range, 0.05f, 0.6f);
    MUTATE_FIELD(social_factor, -1.0f, 1.0f);
    MUTATE_FIELD_SLOW(merge_affinity, 0.0f, 0.5f);
    
    if (rand_float() < mutation_chance * 0.3f) {  // Rare
        int change = rand_range(-1, 1);
        int new_val = (int)genome->max_tracked + change;
        genome->max_tracked = (uint8_t)utils_clamp_i(new_val, 1, 4);
    }
    
    // === Environmental Sensing ===
    MUTATE_FIELD(nutrient_sensitivity, 0.0f, 1.0f);
    MUTATE_FIELD(toxin_sensitivity, 0.0f, 1.0f);
    MUTATE_FIELD_SLOW(edge_affinity, -1.0f, 1.0f);
    MUTATE_FIELD(density_tolerance, 0.0f, 1.0f);
    MUTATE_FIELD_SLOW(quorum_threshold, 0.0f, 1.0f);
    
    // === Colony Interactions ===
    MUTATE_FIELD_SLOW(toxin_production, 0.0f, 1.0f);
    MUTATE_FIELD(toxin_resistance, 0.0f, 1.0f);
    MUTATE_FIELD(signal_emission, 0.0f, 1.0f);
    MUTATE_FIELD(signal_sensitivity, 0.0f, 1.0f);
    MUTATE_FIELD(alarm_threshold, 0.0f, 1.0f);
    MUTATE_FIELD_SLOW(gene_transfer_rate, 0.0f, 0.1f);
    
    // === Competitive Strategy ===
    MUTATE_FIELD(resource_consumption, 0.0f, 1.0f);
    MUTATE_FIELD_SLOW(defense_priority, 0.0f, 1.0f);
    
    // === Survival Strategies ===
    MUTATE_FIELD_SLOW(dormancy_threshold, 0.0f, 0.5f);
    MUTATE_FIELD(dormancy_resistance, 0.0f, 1.0f);
    MUTATE_FIELD_SLOW(biofilm_investment, 0.0f, 1.0f);
    MUTATE_FIELD(motility, 0.0f, 0.5f);
    
    if (rand_float() < mutation_chance) {
        genome->motility_direction += (rand_float() - 0.5f) * 0.5f;
        if (genome->motility_direction < 0) genome->motility_direction += 2.0f * (float)M_PI;
        if (genome->motility_direction > 2.0f * (float)M_PI) genome->motility_direction -= 2.0f * (float)M_PI;
    }
    
    // === Metabolic Strategy ===
    MUTATE_FIELD(efficiency, 0.0f, 1.0f);
    
    // === Neural Network Decision Layer ===
    for (int i = 0; i < 8; i++) {
        if (rand_float() < mutation_chance * 0.5f) {
            float delta = (rand_float() - 0.5f) * MUTATION_DELTA;
            genome->hidden_weights[i] = utils_clamp_f(genome->hidden_weights[i] + delta, -1.0f, 1.0f);
        }
    }
    MUTATE_FIELD_SLOW(learning_rate, 0.0f, 1.0f);
    MUTATE_FIELD_SLOW(memory_factor, 0.0f, 1.0f);

    for (int sensor = 0; sensor < COLONY_SENSOR_COUNT; sensor++) {
        if (rand_float() < mutation_chance * 0.4f) {
            float delta = (rand_float() - 0.5f) * MUTATION_DELTA;
            genome->behavior_sensor_gains[sensor] = utils_clamp_f(genome->behavior_sensor_gains[sensor] + delta, 0.0f, 1.0f);
        }
    }

    for (int drive = 0; drive < COLONY_DRIVE_COUNT; drive++) {
        if (rand_float() < mutation_chance * 0.35f) {
            float delta = (rand_float() - 0.5f) * MUTATION_DELTA;
            genome->behavior_drive_biases[drive] = clamp_signed(genome->behavior_drive_biases[drive] + delta);
        }
        for (int sensor = 0; sensor < COLONY_SENSOR_COUNT; sensor++) {
            if (rand_float() < mutation_chance * 0.25f) {
                float delta = (rand_float() - 0.5f) * MUTATION_DELTA;
                genome->behavior_drive_weights[drive][sensor] = clamp_signed(genome->behavior_drive_weights[drive][sensor] + delta);
            }
        }
    }

    for (int action = 0; action < COLONY_ACTION_COUNT; action++) {
        if (rand_float() < mutation_chance * 0.35f) {
            float delta = (rand_float() - 0.5f) * MUTATION_DELTA;
            genome->behavior_action_biases[action] = clamp_signed(genome->behavior_action_biases[action] + delta);
        }
        for (int drive = 0; drive < COLONY_DRIVE_COUNT; drive++) {
            if (rand_float() < mutation_chance * 0.25f) {
                float delta = (rand_float() - 0.5f) * MUTATION_DELTA;
                genome->behavior_action_weights[action][drive] = clamp_signed(genome->behavior_action_weights[action][drive] + delta);
            }
        }
    }
}

#undef MUTATE_FIELD
#undef MUTATE_FIELD_SLOW

float genome_distance(const Genome* a, const Genome* b) {
    if (!a || !b) return 1.0f;
    
    float diff = 0.0f;
    
    // Basic traits
    diff += utils_abs_f(a->spread_rate - b->spread_rate);
    diff += utils_abs_f(a->mutation_rate - b->mutation_rate) * 5.0f;  // Scale up small range
    diff += utils_abs_f(a->aggression - b->aggression);
    diff += utils_abs_f(a->resilience - b->resilience);
    diff += utils_abs_f(a->metabolism - b->metabolism);
    
    // Social traits
    diff += utils_abs_f(a->detection_range - b->detection_range);
    diff += utils_abs_f(a->social_factor - b->social_factor) * 0.5f;
    diff += utils_abs_f(a->merge_affinity - b->merge_affinity);
    diff += (float)abs((int)a->max_tracked - (int)b->max_tracked) / 4.0f;
    
    // Environmental sensing
    diff += utils_abs_f(a->nutrient_sensitivity - b->nutrient_sensitivity);
    diff += utils_abs_f(a->toxin_sensitivity - b->toxin_sensitivity);
    diff += utils_abs_f(a->edge_affinity - b->edge_affinity) * 0.5f;
    diff += utils_abs_f(a->density_tolerance - b->density_tolerance);
    diff += utils_abs_f(a->quorum_threshold - b->quorum_threshold);
    
    // Colony interactions
    diff += utils_abs_f(a->toxin_production - b->toxin_production);
    diff += utils_abs_f(a->toxin_resistance - b->toxin_resistance);
    diff += utils_abs_f(a->signal_emission - b->signal_emission);
    diff += utils_abs_f(a->signal_sensitivity - b->signal_sensitivity);
    diff += utils_abs_f(a->alarm_threshold - b->alarm_threshold);
    diff += utils_abs_f(a->gene_transfer_rate - b->gene_transfer_rate) * 10.0f;
    
    // Competitive strategy
    diff += utils_abs_f(a->resource_consumption - b->resource_consumption);
    diff += utils_abs_f(a->defense_priority - b->defense_priority);
    
    // Survival strategies
    diff += utils_abs_f(a->dormancy_threshold - b->dormancy_threshold);
    diff += utils_abs_f(a->biofilm_investment - b->biofilm_investment);
    diff += utils_abs_f(a->motility - b->motility);
    diff += utils_abs_f(a->efficiency - b->efficiency);
    
    // Neural network weights (averaged difference, scaled)
    float nn_diff = 0.0f;
    for (int i = 0; i < 8; i++) {
        nn_diff += utils_abs_f(a->hidden_weights[i] - b->hidden_weights[i]);
    }
    diff += nn_diff / 8.0f * 0.5f;  // Average diff scaled by 0.5 (range is -1 to 1)
    diff += utils_abs_f(a->learning_rate - b->learning_rate);
    diff += utils_abs_f(a->memory_factor - b->memory_factor);
    
    float legacy_normalized = diff / GENOME_DISTANCE_WEIGHT_SUM;

    float graph_sensor_diff = average_abs_unit_diff(a->behavior_sensor_gains, b->behavior_sensor_gains,
                                                    COLONY_SENSOR_COUNT);
    float graph_drive_bias_diff = average_abs_signed_diff(a->behavior_drive_biases, b->behavior_drive_biases,
                                                          COLONY_DRIVE_COUNT);
    float graph_drive_weight_diff = average_abs_signed_diff(&a->behavior_drive_weights[0][0],
                                                            &b->behavior_drive_weights[0][0],
                                                            COLONY_DRIVE_COUNT * COLONY_SENSOR_COUNT);
    float graph_action_bias_diff = average_abs_signed_diff(a->behavior_action_biases, b->behavior_action_biases,
                                                           COLONY_ACTION_COUNT);
    float graph_action_weight_diff = average_abs_signed_diff(&a->behavior_action_weights[0][0],
                                                             &b->behavior_action_weights[0][0],
                                                             COLONY_ACTION_COUNT * COLONY_DRIVE_COUNT);
    float graph_normalized = (graph_sensor_diff + graph_drive_bias_diff + graph_drive_weight_diff +
                              graph_action_bias_diff + graph_action_weight_diff) / 5.0f;

    return utils_clamp_f(legacy_normalized * LEGACY_DISTANCE_WEIGHT +
                         graph_normalized * GRAPH_DISTANCE_WEIGHT,
                         0.0f,
                         1.0f);
}

// Helper macro for merging
#define MERGE_FIELD(field) result.field = a->field * weight_a + b->field * weight_b

Genome genome_merge(const Genome* a, size_t count_a, const Genome* b, size_t count_b) {
    Genome result;
    
    if (!a && !b) return genome_create_random();
    if (!a || count_a == 0) return *b;
    if (!b || count_b == 0) return *a;
    
    float total = (float)(count_a + count_b);
    float weight_a = (float)count_a / total;
    float weight_b = (float)count_b / total;
    
    // Spread weights
    for (int i = 0; i < 8; i++) {
        result.spread_weights[i] = a->spread_weights[i] * weight_a + b->spread_weights[i] * weight_b;
    }
    
    // Basic traits
    MERGE_FIELD(spread_rate);
    MERGE_FIELD(mutation_rate);
    MERGE_FIELD(aggression);
    MERGE_FIELD(resilience);
    MERGE_FIELD(metabolism);
    
    // Social traits
    MERGE_FIELD(detection_range);
    result.max_tracked = (uint8_t)(a->max_tracked * weight_a + b->max_tracked * weight_b + 0.5f);
    if (result.max_tracked < 1) result.max_tracked = 1;
    MERGE_FIELD(social_factor);
    MERGE_FIELD(merge_affinity);
    
    // Environmental sensing
    MERGE_FIELD(nutrient_sensitivity);
    MERGE_FIELD(toxin_sensitivity);
    MERGE_FIELD(edge_affinity);
    MERGE_FIELD(density_tolerance);
    MERGE_FIELD(quorum_threshold);
    
    // Colony interactions
    MERGE_FIELD(toxin_production);
    MERGE_FIELD(toxin_resistance);
    MERGE_FIELD(signal_emission);
    MERGE_FIELD(signal_sensitivity);
    MERGE_FIELD(alarm_threshold);
    MERGE_FIELD(gene_transfer_rate);
    
    // Competitive strategy
    MERGE_FIELD(resource_consumption);
    MERGE_FIELD(defense_priority);
    
    // Survival strategies
    MERGE_FIELD(dormancy_threshold);
    MERGE_FIELD(dormancy_resistance);
    MERGE_FIELD(sporulation_threshold);
    MERGE_FIELD(biofilm_investment);
    MERGE_FIELD(biofilm_tendency);
    MERGE_FIELD(motility);
    // Average motility direction using circular mean
    float sin_sum = sinf(a->motility_direction) * weight_a + sinf(b->motility_direction) * weight_b;
    float cos_sum = cosf(a->motility_direction) * weight_a + cosf(b->motility_direction) * weight_b;
    result.motility_direction = atan2f(sin_sum, cos_sum);
    if (result.motility_direction < 0) result.motility_direction += 2.0f * (float)M_PI;
    MERGE_FIELD(specialization);
    
    // Metabolic
    MERGE_FIELD(efficiency);

    // Legacy neural layer
    for (int i = 0; i < 8; i++) {
        result.hidden_weights[i] = a->hidden_weights[i] * weight_a + b->hidden_weights[i] * weight_b;
    }
    MERGE_FIELD(learning_rate);
    MERGE_FIELD(memory_factor);

    // Explicit behavior graph genes
    for (int sensor = 0; sensor < COLONY_SENSOR_COUNT; sensor++) {
        result.behavior_sensor_gains[sensor] = a->behavior_sensor_gains[sensor] * weight_a +
                                               b->behavior_sensor_gains[sensor] * weight_b;
    }
    for (int drive = 0; drive < COLONY_DRIVE_COUNT; drive++) {
        result.behavior_drive_biases[drive] = a->behavior_drive_biases[drive] * weight_a +
                                              b->behavior_drive_biases[drive] * weight_b;
        for (int sensor = 0; sensor < COLONY_SENSOR_COUNT; sensor++) {
            result.behavior_drive_weights[drive][sensor] = a->behavior_drive_weights[drive][sensor] * weight_a +
                                                           b->behavior_drive_weights[drive][sensor] * weight_b;
        }
    }
    for (int action = 0; action < COLONY_ACTION_COUNT; action++) {
        result.behavior_action_biases[action] = a->behavior_action_biases[action] * weight_a +
                                                b->behavior_action_biases[action] * weight_b;
        for (int drive = 0; drive < COLONY_DRIVE_COUNT; drive++) {
            result.behavior_action_weights[action][drive] = a->behavior_action_weights[action][drive] * weight_a +
                                                            b->behavior_action_weights[action][drive] * weight_b;
        }
    }
    
    // Blend colors
    result.body_color.r = (uint8_t)(a->body_color.r * weight_a + b->body_color.r * weight_b);
    result.body_color.g = (uint8_t)(a->body_color.g * weight_a + b->body_color.g * weight_b);
    result.body_color.b = (uint8_t)(a->body_color.b * weight_a + b->body_color.b * weight_b);
    result.border_color.r = (uint8_t)(a->border_color.r * weight_a + b->border_color.r * weight_b);
    result.border_color.g = (uint8_t)(a->border_color.g * weight_a + b->border_color.g * weight_b);
    result.border_color.b = (uint8_t)(a->border_color.b * weight_a + b->border_color.b * weight_b);
    
    return result;
}

#undef MERGE_FIELD

bool genome_compatible(const Genome* a, const Genome* b, float threshold) {
    if (!a || !b) return false;
    return genome_distance(a, b) <= threshold;
}

// Horizontal gene transfer - transfer some traits from donor to recipient
void genome_transfer_genes(Genome* recipient, const Genome* donor, float transfer_strength) {
    if (!recipient || !donor || transfer_strength <= 0) return;
    
    // Randomly select traits to transfer
    if (rand_float() < 0.3f) {
        recipient->toxin_resistance += (donor->toxin_resistance - recipient->toxin_resistance) * transfer_strength;
    }
    if (rand_float() < 0.3f) {
        recipient->nutrient_sensitivity += (donor->nutrient_sensitivity - recipient->nutrient_sensitivity) * transfer_strength;
    }
    if (rand_float() < 0.2f) {
        recipient->efficiency += (donor->efficiency - recipient->efficiency) * transfer_strength;
    }
    if (rand_float() < 0.2f) {
        recipient->dormancy_resistance += (donor->dormancy_resistance - recipient->dormancy_resistance) * transfer_strength;
    }

    if (rand_float() < 0.35f) {
        int drive = rand_range(0, COLONY_DRIVE_COUNT - 1);
        recipient->behavior_drive_biases[drive] +=
            (donor->behavior_drive_biases[drive] - recipient->behavior_drive_biases[drive]) * transfer_strength;
        for (int sensor = 0; sensor < COLONY_SENSOR_COUNT; sensor++) {
            recipient->behavior_drive_weights[drive][sensor] +=
                (donor->behavior_drive_weights[drive][sensor] - recipient->behavior_drive_weights[drive][sensor]) * transfer_strength;
        }
    }

    if (rand_float() < 0.35f) {
        int action = rand_range(0, COLONY_ACTION_COUNT - 1);
        recipient->behavior_action_biases[action] +=
            (donor->behavior_action_biases[action] - recipient->behavior_action_biases[action]) * transfer_strength;
        for (int drive = 0; drive < COLONY_DRIVE_COUNT; drive++) {
            recipient->behavior_action_weights[action][drive] +=
                (donor->behavior_action_weights[action][drive] - recipient->behavior_action_weights[action][drive]) * transfer_strength;
        }
    }
}
