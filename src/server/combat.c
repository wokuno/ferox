#include "combat.h"
#include "../shared/simulation_common.h"
#include "../shared/utils.h"
#include <stdlib.h>
#include <string.h>

static const int DX[] = {0, 1, 0, -1};
static const int DY[] = {-1, 0, 1, 0};

void simulation_resolve_combat(World* world) {
    if (!world) return;
    
    if (world->toxins) {
        int total = world->width * world->height;
        for (int i = 0; i < total; i++) {
            world->toxins[i] *= 0.95f;
            if (world->toxins[i] < 0.001f) world->toxins[i] = 0.0f;
        }
    }
    
    typedef struct { int x, y; uint32_t winner; uint32_t loser; float strength; } CombatResult;
    CombatResult* results = NULL;
    int result_count = 0;
    int result_capacity = 128;
    results = (CombatResult*)malloc(result_capacity * sizeof(CombatResult));
    if (!results) return;
    
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            Cell* cell = world_get_cell(world, x, y);
            if (!cell || cell->colony_id == 0 || !cell->is_border) continue;
            
            Colony* colony = world_get_colony(world, cell->colony_id);
            if (!colony || !colony->active) continue;
            
            float quorum_activation = get_quorum_activation(colony);
            float toxin_emit = colony->genome.toxin_production *
                               (0.06f + 0.06f * quorum_activation);
            if (colony->is_dormant) toxin_emit *= 0.5f;
            if (toxin_emit > 0.01f) {
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
    
    for (int y = 0; y < world->height; y++) {
        for (int x = 0; x < world->width; x++) {
            Cell* cell = world_get_cell(world, x, y);
            if (!cell || cell->colony_id == 0 || !cell->is_border) continue;
            
            Colony* attacker = world_get_colony(world, cell->colony_id);
            if (!attacker || !attacker->active) continue;
            
            if (attacker->stress_level > 0.7f && attacker->genome.defense_priority > 0.6f) {
                continue;
            }
            
            int attacker_friendly = count_friendly_neighbors(world, x, y, cell->colony_id);
            
            for (int d = 0; d < 4; d++) {
                int nx = x + DX[d];
                int ny = y + DY[d];
                
                Cell* neighbor = world_get_cell(world, nx, ny);
                if (!neighbor || neighbor->colony_id == 0 || 
                    neighbor->colony_id == cell->colony_id) continue;
                
                Colony* defender = world_get_colony(world, neighbor->colony_id);
                if (!defender || !defender->active) continue;
                
                float attack_str = attacker->genome.aggression * 1.2f;
                float defend_str = defender->genome.resilience * 1.0f;
                
                float flanking_bonus = 1.0f + (attacker_friendly * 0.15f);
                attack_str *= flanking_bonus;
                
                int defender_friendly = count_friendly_neighbors(world, nx, ny, neighbor->colony_id);
                float defensive_bonus = 1.0f + (defender->genome.defense_priority * defender_friendly * 0.2f);
                defend_str *= defensive_bonus;
                
                float dir_weight = get_direction_weight(&attacker->genome, DX[d], DY[d]);
                attack_str *= (0.7f + dir_weight * 0.6f);
                
                attack_str += attacker->genome.toxin_production * 0.4f;
                defend_str += defender->genome.toxin_resistance * 0.3f;
                
                defend_str *= (1.0f + defender->biofilm_strength * 0.3f);
                
                if (world->nutrients) {
                    int attacker_idx = y * world->width + x;
                    int defender_idx = ny * world->width + nx;
                    attack_str *= (0.6f + world->nutrients[attacker_idx] * 0.5f);
                    defend_str *= (0.6f + world->nutrients[defender_idx] * 0.5f);
                    
                    attack_str *= (1.0f - world->toxins[attacker_idx] * (1.0f - attacker->genome.toxin_resistance));
                    defend_str *= (1.0f - world->toxins[defender_idx] * (1.0f - defender->genome.toxin_resistance));
                }
                
                attack_str *= (0.8f + attacker->success_history[d] * 0.4f);
                
                if (attacker->stress_level > 0.5f) {
                    attack_str *= (1.0f + attacker->genome.aggression * 0.3f);
                }
                if (defender->stress_level > 0.5f && defender->genome.defense_priority < 0.4f) {
                    defend_str *= 0.7f;
                }
                
                float size_ratio = (float)attacker->cell_count / (float)(defender->cell_count + 1);
                if (size_ratio > 2.0f) attack_str *= 1.15f;
                if (size_ratio < 0.5f) attack_str *= 0.85f;
                
                float attack_chance = attack_str / (attack_str + defend_str + 0.1f);
                
                float combat_noise = 0.5f + rand_float() * 1.0f;
                
                if (rand_float() < attack_chance * combat_noise) {
                    if (result_count >= result_capacity) {
                        result_capacity *= 2;
                        CombatResult* new_results = (CombatResult*)realloc(results, result_capacity * sizeof(CombatResult));
                        if (!new_results) { free(results); return; }
                        results = new_results;
                    }
                    results[result_count++] = (CombatResult){nx, ny, cell->colony_id, neighbor->colony_id, attack_str};
                    
                    if (d < 8) {
                        attacker->success_history[d] = utils_clamp_f(
                            attacker->success_history[d] + 0.05f * attacker->genome.learning_rate, 0.0f, 1.0f);
                    }
                } else if (rand_float() < 0.3f) {
                    if (d < 8) {
                        attacker->success_history[d] = utils_clamp_f(
                            attacker->success_history[d] - 0.02f * attacker->genome.learning_rate, 0.0f, 1.0f);
                    }
                }
            }
        }
    }
    
    for (int i = 0; i < result_count; i++) {
        Cell* cell = world_get_cell(world, results[i].x, results[i].y);
        if (cell && cell->colony_id == results[i].loser) {
            Colony* loser = world_get_colony(world, results[i].loser);
            Colony* winner = world_get_colony(world, results[i].winner);
            
            if (loser && loser->cell_count > 0) {
                loser->cell_count--;
                loser->stress_level = utils_clamp_f(loser->stress_level + 0.01f, 0.0f, 1.0f);
            }
            
            if (winner && winner->active) {
                cell->colony_id = results[i].winner;
                cell->age = 0;
                cell->is_border = true;
                winner->cell_count++;
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
