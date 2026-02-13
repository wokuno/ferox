#include "genetics.h"
#include "../shared/utils.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Much larger mutations for dynamic evolution
#define MUTATION_DELTA 0.25f
#define MUTATION_DELTA_LARGE 0.4f  // For dramatic trait shifts
#define NUM_GENOME_FIELDS 26  // All evolvable traits (added neural network + env sensing fields)
// Basic (5): spread_rate(1) + mutation_rate*5(0.5) + aggression(1) + resilience(1) + metabolism(1) = 4.5
// Social (4): detection_range(1) + social_factor*0.5(1) + merge_affinity(1) + max_tracked(0.75) = 3.75
// Environmental (5): nutrient_sensitivity(1) + toxin_sensitivity(1) + edge_affinity*0.5(1) + density_tolerance(1) + quorum_threshold(1) = 5
// Colony interactions (6): toxin_production(1) + toxin_resistance(1) + signal_emission(1) + signal_sensitivity(1) + alarm_threshold(1) + gene_transfer_rate*10(1) = 6
// Competitive (2): resource_consumption(1) + defense_priority(1) = 2
// Survival (4): dormancy_threshold(1) + biofilm_investment(1) + motility(1) + efficiency(1) = 4
// Neural (3): hidden_weights avg*0.5(1) + learning_rate(1) + memory_factor(1) = 3
// Total: 4.5 + 3.75 + 5 + 6 + 2 + 4 + 3 = 28.25
#define GENOME_DISTANCE_WEIGHT_SUM 28.25f

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

Genome genome_create_random(void) {
    Genome g;
    memset(&g, 0, sizeof(Genome));
    
    // === STRATEGY ARCHETYPE: Pick a dominant strategy ===
    // This creates VASTLY different colony behaviors
    int strategy = rand() % 8;
    
    // Base randomness for all traits
    float chaos = rand_float();  // 0-1: how "wild" the colony is
    
    // Strategy-specific biases
    float aggression_bias = 0.5f;
    float growth_bias = 0.5f;
    float social_bias = 0.5f;
    float toxin_bias = 0.5f;
    float defense_bias = 0.5f;
    float mobility_bias = 0.5f;
    
    switch (strategy) {
        case 0:  // BERSERKER: Maximum aggression, charge at everything
            aggression_bias = 0.9f + rand_float() * 0.1f;
            growth_bias = 0.8f + rand_float() * 0.2f;
            defense_bias = rand_float() * 0.3f;  // Glass cannon
            toxin_bias = 0.7f + rand_float() * 0.3f;
            social_bias = rand_float() * 0.2f;  // Loner
            mobility_bias = 0.8f + rand_float() * 0.2f;
            break;
        case 1:  // TURTLE: Maximum defense, slow but steady
            aggression_bias = rand_float() * 0.2f;
            defense_bias = 0.9f + rand_float() * 0.1f;
            growth_bias = 0.2f + rand_float() * 0.3f;  // Slow growth
            toxin_bias = 0.3f + rand_float() * 0.3f;
            social_bias = 0.5f + rand_float() * 0.3f;
            mobility_bias = rand_float() * 0.2f;  // Stay put
            break;
        case 2:  // SWARM: Fast spreading, overwhelm with numbers
            growth_bias = 0.95f + rand_float() * 0.05f;
            aggression_bias = 0.5f + rand_float() * 0.3f;
            defense_bias = rand_float() * 0.4f;  // Weak individually
            toxin_bias = rand_float() * 0.3f;
            social_bias = rand_float() * 0.3f;
            mobility_bias = 0.6f + rand_float() * 0.3f;
            break;
        case 3:  // TOXIC: Poison everything, chemical warfare
            toxin_bias = 0.95f + rand_float() * 0.05f;
            aggression_bias = 0.4f + rand_float() * 0.4f;
            defense_bias = 0.5f + rand_float() * 0.3f;
            growth_bias = 0.3f + rand_float() * 0.4f;
            social_bias = rand_float() * 0.3f;
            mobility_bias = 0.3f + rand_float() * 0.3f;
            break;
        case 4:  // HIVE: Highly social, cooperative expansion
            social_bias = 0.9f + rand_float() * 0.1f;
            growth_bias = 0.5f + rand_float() * 0.3f;
            aggression_bias = 0.3f + rand_float() * 0.4f;
            defense_bias = 0.5f + rand_float() * 0.3f;
            toxin_bias = rand_float() * 0.4f;
            mobility_bias = 0.4f + rand_float() * 0.3f;
            break;
        case 5:  // NOMAD: High mobility, constantly shifting
            mobility_bias = 0.95f + rand_float() * 0.05f;
            growth_bias = 0.6f + rand_float() * 0.3f;
            aggression_bias = 0.4f + rand_float() * 0.4f;
            defense_bias = rand_float() * 0.4f;
            toxin_bias = rand_float() * 0.3f;
            social_bias = rand_float() * 0.5f;
            break;
        case 6:  // PARASITE: Weak alone, steals from others
            aggression_bias = 0.7f + rand_float() * 0.3f;
            growth_bias = 0.4f + rand_float() * 0.3f;
            defense_bias = rand_float() * 0.3f;
            toxin_bias = 0.2f + rand_float() * 0.3f;
            social_bias = 0.6f + rand_float() * 0.3f;  // Seeks others
            mobility_bias = 0.5f + rand_float() * 0.3f;
            break;
        default:  // CHAOTIC: Completely random, unpredictable
            aggression_bias = rand_float();
            growth_bias = rand_float();
            defense_bias = rand_float();
            toxin_bias = rand_float();
            social_bias = rand_float();
            mobility_bias = rand_float();
            chaos = 0.8f + rand_float() * 0.2f;  // Extra chaotic
            break;
    }
    
    // Add chaos factor to all biases
    aggression_bias = utils_clamp_f(aggression_bias + (rand_float() - 0.5f) * chaos * 0.4f, 0.0f, 1.0f);
    growth_bias = utils_clamp_f(growth_bias + (rand_float() - 0.5f) * chaos * 0.4f, 0.0f, 1.0f);
    defense_bias = utils_clamp_f(defense_bias + (rand_float() - 0.5f) * chaos * 0.4f, 0.0f, 1.0f);
    
    // === SPREAD WEIGHTS: Mild directional preferences for organic shapes ===
    // All directions start with a strong base; slight variation creates gentle asymmetry
    for (int i = 0; i < 8; i++) {
        g.spread_weights[i] = 0.7f + rand_float() * 0.3f;  // 0.7-1.0 base
    }
    // 1-2 slightly preferred directions
    int strong_dirs = 1 + rand() % 2;
    for (int i = 0; i < strong_dirs; i++) {
        int dir = rand() % 8;
        g.spread_weights[dir] = 0.9f + rand_float() * 0.1f;
    }
    
    // === CORE TRAITS: Heavily influenced by strategy ===
    g.spread_rate = utils_clamp_f(growth_bias * 0.8f + rand_float() * 0.2f, 0.1f, 1.0f);
    g.mutation_rate = 0.05f + rand_float() * 0.3f + chaos * 0.1f;  // Chaotic = more mutation
    g.aggression = utils_clamp_f(aggression_bias * 0.9f + rand_float() * 0.1f, 0.0f, 1.0f);
    g.resilience = utils_clamp_f(defense_bias * 0.9f + rand_float() * 0.1f, 0.0f, 1.0f);
    g.metabolism = utils_clamp_f(growth_bias * 0.7f + rand_float() * 0.3f, 0.2f, 1.0f);
    
    // === SOCIAL BEHAVIOR: Extreme variance ===
    g.detection_range = utils_clamp_f(rand_float() * 0.7f + social_bias * 0.3f, 0.0f, 1.0f);
    g.max_tracked = (uint8_t)(1 + (int)(social_bias * 5));
    g.social_factor = utils_clamp_f((social_bias - 0.5f) * 2.5f + (rand_float() - 0.5f) * 0.5f, -1.0f, 1.0f);
    g.merge_affinity = social_bias * 0.6f + rand_float() * 0.2f;
    
    // === ENVIRONMENTAL SENSING ===
    g.nutrient_sensitivity = rand_float();
    g.toxin_sensitivity = utils_clamp_f(1.0f - toxin_bias * 0.5f + rand_float() * 0.3f, 0.0f, 1.0f);
    g.edge_affinity = (rand_float() - 0.5f) * 2.0f + (mobility_bias - 0.5f);
    g.density_tolerance = utils_clamp_f(social_bias * 0.6f + rand_float() * 0.4f, 0.0f, 1.0f);
    g.quorum_threshold = rand_float() * 0.8f;
    
    // === COLONY INTERACTIONS: Strategy-driven ===
    g.toxin_production = utils_clamp_f(toxin_bias * 0.9f + rand_float() * 0.1f, 0.0f, 1.0f);
    g.toxin_resistance = utils_clamp_f(toxin_bias * 0.7f + rand_float() * 0.3f, 0.0f, 1.0f);
    g.signal_emission = utils_clamp_f(social_bias * 0.7f + rand_float() * 0.3f, 0.0f, 1.0f);
    g.signal_sensitivity = utils_clamp_f(social_bias * 0.5f + mobility_bias * 0.3f + rand_float() * 0.2f, 0.0f, 1.0f);
    g.alarm_threshold = rand_float() * 0.8f;
    g.gene_transfer_rate = social_bias * 0.15f + rand_float() * 0.05f;
    
    // === COMPETITIVE STRATEGY ===
    g.resource_consumption = utils_clamp_f(aggression_bias * 0.5f + growth_bias * 0.4f + rand_float() * 0.1f, 0.0f, 1.0f);
    g.defense_priority = utils_clamp_f(defense_bias * 0.9f + rand_float() * 0.1f, 0.0f, 1.0f);
    
    // === SURVIVAL STRATEGIES ===
    g.dormancy_threshold = defense_bias * 0.3f + rand_float() * 0.2f;
    g.dormancy_resistance = defense_bias * 0.6f + rand_float() * 0.4f;
    g.sporulation_threshold = 0.2f + defense_bias * 0.4f + rand_float() * 0.2f;
    g.biofilm_investment = defense_bias * 0.5f + rand_float() * 0.3f;
    g.biofilm_tendency = defense_bias * 0.6f + rand_float() * 0.2f;
    g.motility = utils_clamp_f(mobility_bias * 0.8f + rand_float() * 0.2f, 0.0f, 1.0f);
    g.motility_direction = rand_float() * 2.0f * (float)M_PI;
    g.specialization = rand_float() * 0.8f;
    
    // === METABOLIC STRATEGY ===
    g.efficiency = utils_clamp_f(0.2f + (1.0f - growth_bias) * 0.5f + rand_float() * 0.3f, 0.0f, 1.0f);
    
    // === NEURAL NETWORK: Wild weights for unpredictable behavior ===
    for (int i = 0; i < 8; i++) {
        float w = (rand_float() - 0.5f) * 2.0f;
        // 30% chance of EXTREME weight
        if (rand_float() < 0.3f) {
            w = w > 0 ? 0.9f + rand_float() * 0.1f : -0.9f - rand_float() * 0.1f;
        }
        // Chaos adds more variance
        w += (rand_float() - 0.5f) * chaos * 0.5f;
        g.hidden_weights[i] = utils_clamp_f(w, -1.0f, 1.0f);
    }
    g.learning_rate = 0.1f + rand_float() * 0.5f + chaos * 0.2f;  // Chaotic = learns faster
    g.memory_factor = 0.2f + rand_float() * 0.6f;
    
    // === COLORS: Strategy-influenced with high saturation ===
    // Make colors more vibrant and strategy-correlated
    int base_hue = rand() % 360;
    float saturation = 0.7f + rand_float() * 0.3f;
    float value = 0.6f + rand_float() * 0.4f;
    
    // HSV to RGB conversion
    float h = (float)base_hue / 60.0f;
    int hi = (int)h % 6;
    float f = h - (float)hi;
    float p = value * (1.0f - saturation);
    float q = value * (1.0f - f * saturation);
    float t = value * (1.0f - (1.0f - f) * saturation);
    
    float r, gr, b;
    switch (hi) {
        case 0: r = value; gr = t; b = p; break;
        case 1: r = q; gr = value; b = p; break;
        case 2: r = p; gr = value; b = t; break;
        case 3: r = p; gr = q; b = value; break;
        case 4: r = t; gr = p; b = value; break;
        default: r = value; gr = p; b = q; break;
    }
    
    // Ensure minimum brightness of 30 for all channels
    g.body_color.r = (uint8_t)(30 + (uint8_t)(r * 225));
    g.body_color.g = (uint8_t)(30 + (uint8_t)(gr * 225));
    g.body_color.b = (uint8_t)(30 + (uint8_t)(b * 225));
    g.border_color.r = (uint8_t)(g.body_color.r * 0.5f);
    g.border_color.g = (uint8_t)(g.body_color.g * 0.5f);
    g.border_color.b = (uint8_t)(g.body_color.b * 0.5f);
    
    return g;
}

// Helper macro for mutation - now with larger changes
#define MUTATE_FIELD(field, min_val, max_val) \
    if (rand_float() < mutation_chance) { \
        float delta = (rand_float() - 0.5f) * 2.0f * MUTATION_DELTA; \
        genome->field = utils_clamp_f(genome->field + delta, min_val, max_val); \
    }

// Large mutations for key traits that drive behavior
#define MUTATE_FIELD_LARGE(field, min_val, max_val) \
    if (rand_float() < mutation_chance * 1.5f) { \
        float delta = (rand_float() - 0.5f) * 2.0f * MUTATION_DELTA_LARGE; \
        genome->field = utils_clamp_f(genome->field + delta, min_val, max_val); \
    }

#define MUTATE_FIELD_SLOW(field, min_val, max_val) \
    if (rand_float() < mutation_chance * 0.7f) { \
        float delta = (rand_float() - 0.5f) * MUTATION_DELTA; \
        genome->field = utils_clamp_f(genome->field + delta, min_val, max_val); \
    }

void genome_mutate(Genome* genome) {
    if (!genome) return;
    
    // Base mutation chance from genome, but with a floor for constant evolution
    float mutation_chance = fmaxf(genome->mutation_rate, 0.08f);
    
    // Occasional "hypermutation" events - dramatic genetic shifts (5% chance)
    bool hypermutation = rand_float() < 0.05f;
    if (hypermutation) {
        mutation_chance *= 4.0f;  // Quadruple mutation rate during hypermutation
    }
    
    // Rare "radical mutation" - completely randomize one trait (1% chance)
    if (rand_float() < 0.01f) {
        int trait = rand() % 10;
        switch (trait) {
            case 0: genome->spread_rate = rand_float(); break;
            case 1: genome->aggression = rand_float(); break;
            case 2: genome->resilience = rand_float(); break;
            case 3: genome->metabolism = rand_float(); break;
            case 4: genome->toxin_production = rand_float(); break;
            case 5: genome->toxin_resistance = rand_float(); break;
            case 6: genome->motility = rand_float() * 0.6f; break;
            case 7: genome->social_factor = (rand_float() - 0.5f) * 2.0f; break;
            case 8: genome->efficiency = rand_float(); break;
            case 9: genome->defense_priority = rand_float(); break;
        }
    }
    
    // === Basic Traits - these drive core behavior ===
    MUTATE_FIELD_LARGE(spread_rate, 0.0f, 1.0f);
    MUTATE_FIELD(mutation_rate, 0.01f, 0.4f);  // Can get very high mutation
    MUTATE_FIELD_LARGE(aggression, 0.0f, 1.0f);
    MUTATE_FIELD_LARGE(resilience, 0.0f, 1.0f);
    MUTATE_FIELD_LARGE(metabolism, 0.2f, 1.0f);   // Always some metabolism
    
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
    
    // === Competitive Strategy - key for dynamic battles ===
    MUTATE_FIELD_LARGE(resource_consumption, 0.0f, 1.0f);
    MUTATE_FIELD_LARGE(defense_priority, 0.0f, 1.0f);
    
    // === Survival Strategies ===
    MUTATE_FIELD_SLOW(dormancy_threshold, 0.0f, 0.5f);
    MUTATE_FIELD(dormancy_resistance, 0.0f, 1.0f);
    MUTATE_FIELD(biofilm_investment, 0.0f, 1.0f);
    MUTATE_FIELD_LARGE(motility, 0.0f, 0.8f);  // Higher max motility
    
    if (rand_float() < mutation_chance) {
        genome->motility_direction += (rand_float() - 0.5f) * 0.5f;
        if (genome->motility_direction < 0) genome->motility_direction += 2.0f * (float)M_PI;
        if (genome->motility_direction > 2.0f * (float)M_PI) genome->motility_direction -= 2.0f * (float)M_PI;
    }
    
    // === Metabolic Strategy ===
    MUTATE_FIELD_LARGE(efficiency, 0.0f, 1.0f);
    
    // === Neural Network Decision Layer - more active learning ===
    for (int i = 0; i < 8; i++) {
        if (rand_float() < mutation_chance) {
            float delta = (rand_float() - 0.5f) * MUTATION_DELTA_LARGE;
            genome->hidden_weights[i] = utils_clamp_f(genome->hidden_weights[i] + delta, -1.0f, 1.0f);
        }
    }
    MUTATE_FIELD(learning_rate, 0.0f, 1.0f);
    MUTATE_FIELD(memory_factor, 0.0f, 1.0f);
    
    // === Color mutations - visible evolution ===
    if (rand_float() < mutation_chance * 0.3f) {
        int color_shift = rand_range(-30, 30);
        genome->body_color.r = (uint8_t)utils_clamp_i(genome->body_color.r + color_shift, 30, 255);
    }
    if (rand_float() < mutation_chance * 0.3f) {
        int color_shift = rand_range(-30, 30);
        genome->body_color.g = (uint8_t)utils_clamp_i(genome->body_color.g + color_shift, 30, 255);
    }
    if (rand_float() < mutation_chance * 0.3f) {
        int color_shift = rand_range(-30, 30);
        genome->body_color.b = (uint8_t)utils_clamp_i(genome->body_color.b + color_shift, 30, 255);
    }
    // Border color tracks body color
    genome->border_color.r = (uint8_t)(genome->body_color.r / 2);
    genome->border_color.g = (uint8_t)(genome->body_color.g / 2);
    genome->border_color.b = (uint8_t)(genome->body_color.b / 2);
}

#undef MUTATE_FIELD
#undef MUTATE_FIELD_LARGE
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
    
    // Normalize to 0-1 range
    return diff / GENOME_DISTANCE_WEIGHT_SUM;
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
    MERGE_FIELD(biofilm_investment);
    MERGE_FIELD(motility);
    // Average motility direction using circular mean
    float sin_sum = sinf(a->motility_direction) * weight_a + sinf(b->motility_direction) * weight_b;
    float cos_sum = cosf(a->motility_direction) * weight_a + cosf(b->motility_direction) * weight_b;
    result.motility_direction = atan2f(sin_sum, cos_sum);
    if (result.motility_direction < 0) result.motility_direction += 2.0f * (float)M_PI;
    
    // Metabolic
    MERGE_FIELD(efficiency);
    
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
}
