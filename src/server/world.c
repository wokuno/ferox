#include "world.h"
#include "genetics.h"
#include "../shared/utils.h"
#include "../shared/names.h"
#include "../shared/colors.h"
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <limits.h>
#include <stdio.h>

#define INITIAL_COLONY_CAPACITY 16
#define INITIAL_LOOKUP_CAPACITY 32

#define MONOD_DEFAULT_ENABLED false
#define MONOD_DEFAULT_HALF_SATURATION 0.35f
#define MONOD_DEFAULT_UPTAKE_MIN 0.25f
#define MONOD_DEFAULT_UPTAKE_MAX 1.0f
#define MONOD_DEFAULT_GROWTH_COUPLING 0.0f

static RDSolverControls world_default_rd_controls(void) {
    RDSolverControls controls;
    controls.nutrients.diffusion = RD_DEFAULT_NUTRIENT_DIFFUSION;
    controls.nutrients.decay = RD_DEFAULT_NUTRIENT_DECAY;
    controls.toxins.diffusion = RD_DEFAULT_TOXIN_DIFFUSION;
    controls.toxins.decay = RD_DEFAULT_TOXIN_DECAY;
    controls.signals.diffusion = RD_DEFAULT_SIGNAL_DIFFUSION;
    controls.signals.decay = RD_DEFAULT_SIGNAL_DECAY;
    return controls;
}

static bool world_validate_rd_field(const RDFieldControl* field,
                                    const char* field_name,
                                    char* err_buf,
                                    size_t err_buf_size) {
    if (!field || !field_name) {
        return false;
    }

    const float diffusion = field->diffusion;
    const float decay = field->decay;

    if (diffusion < 0.0f || diffusion > RD_FIELD_MAX_DIFFUSION) {
        if (err_buf && err_buf_size > 0) {
            snprintf(err_buf, err_buf_size,
                     "%s diffusion %.4f must be in [0.0, %.2f]",
                     field_name, diffusion, RD_FIELD_MAX_DIFFUSION);
        }
        return false;
    }

    if (decay < 0.0f || decay > 1.0f) {
        if (err_buf && err_buf_size > 0) {
            snprintf(err_buf, err_buf_size,
                     "%s decay %.4f must be in [0.0, 1.0]",
                     field_name, decay);
        }
        return false;
    }

    // Explicit 2D/4-neighbor solver guardrail (dt = 1):
    // center weight = 1 - decay - 4*diffusion must remain non-negative.
    if ((4.0f * diffusion + decay) > 1.0f) {
        if (err_buf && err_buf_size > 0) {
            snprintf(err_buf, err_buf_size,
                     "%s unstable: 4*diffusion + decay = %.4f exceeds 1.0",
                     field_name, 4.0f * diffusion + decay);
        }
        return false;
    }

    return true;
}

static const HGTKinetics DEFAULT_HGT_KINETICS = {
    .contact_rate = 0.6f,
    .donor_transfer_rate = 0.75f,
    .transconjugant_transfer_rate = 0.5f,
    .recipient_uptake_rate = 0.7f,
    .transfer_efficiency = 0.25f,
    .plasmid_cost_per_fraction = 0.12f,
    .plasmid_loss_rate = 0.01f,
    .enable_plasmid_cost = true,
    .enable_plasmid_loss = true,
};

World* world_create(int width, int height) {
    if (width <= 0 || height <= 0) {
        return NULL;
    }

    if (height > INT_MAX / width) {
        return NULL;
    }

    size_t grid_size = (size_t)width * (size_t)height;
    if (grid_size > SIZE_MAX / sizeof(Cell) ||
        grid_size > SIZE_MAX / sizeof(float) ||
        grid_size > SIZE_MAX / sizeof(uint32_t)) {
        return NULL;
    }
    
    World* world = (World*)malloc(sizeof(World));
    if (!world) return NULL;

    memset(world, 0, sizeof(World));
    
    world->width = width;
    world->height = height;
    world->tick = 0;
    world->colony_count = 0;
    world->colony_capacity = INITIAL_COLONY_CAPACITY;
    atomic_init(&world->next_colony_id, 1);
    world->monod.enabled = MONOD_DEFAULT_ENABLED;
    world->monod.half_saturation = MONOD_DEFAULT_HALF_SATURATION;
    world->monod.uptake_min = MONOD_DEFAULT_UPTAKE_MIN;
    world->monod.uptake_max = MONOD_DEFAULT_UPTAKE_MAX;
    world->monod.growth_coupling = MONOD_DEFAULT_GROWTH_COUPLING;
    world->rd_controls = world_default_rd_controls();
    
    world->hgt_kinetics = DEFAULT_HGT_KINETICS;
    memset(&world->hgt_metrics, 0, sizeof(world->hgt_metrics));
    
    // Allocate cells as flat array
    world->cells = (Cell*)calloc(grid_size, sizeof(Cell));
    if (!world->cells) {
        goto fail;
    }
    
    // Initialize all cells
    for (int i = 0; i < width * height; i++) {
        world->cells[i].component_id = -1;
    }
    
    // Allocate environmental layers
    world->nutrients = (float*)malloc(grid_size * sizeof(float));
    if (!world->nutrients) {
        goto fail;
    }
    
    world->toxins = (float*)calloc(grid_size, sizeof(float));
    if (!world->toxins) {
        goto fail;
    }
    
    world->signals = (float*)calloc(grid_size, sizeof(float));
    if (!world->signals) {
        goto fail;
    }
    
    world->alarm_signals = (float*)calloc(grid_size, sizeof(float));
    if (!world->alarm_signals) {
        goto fail;
    }
    
    world->signal_source = (uint32_t*)calloc(grid_size, sizeof(uint32_t));
    if (!world->signal_source) {
        goto fail;
    }
    
    world->alarm_source = (uint32_t*)calloc(grid_size, sizeof(uint32_t));
    if (!world->alarm_source) {
        goto fail;
    }
    
    world->scratch_signals = (float*)calloc(grid_size, sizeof(float));
    if (!world->scratch_signals) {
        goto fail;
    }
    
    world->scratch_sources = (uint32_t*)calloc(grid_size, sizeof(uint32_t));
    if (!world->scratch_sources) {
        goto fail;
    }
    
    // Initialize nutrients with full resources
    for (size_t i = 0; i < grid_size; i++) {
        world->nutrients[i] = 1.0f;
    }
    
    // Allocate colony array
    world->colonies = (Colony*)malloc(world->colony_capacity * sizeof(Colony));
    if (!world->colonies) {
        goto fail;
    }
    
    // Allocate colony lookup table
    world->colony_by_id_capacity = INITIAL_LOOKUP_CAPACITY;
    world->colony_by_id = (Colony**)calloc(world->colony_by_id_capacity, sizeof(Colony*));
    if (!world->colony_by_id) {
        goto fail;
    }
    
    return world;

fail:
    free(world->colony_by_id);
    free(world->colonies);
    free(world->scratch_sources);
    free(world->scratch_signals);
    free(world->alarm_source);
    free(world->signal_source);
    free(world->alarm_signals);
    free(world->signals);
    free(world->toxins);
    free(world->nutrients);
    free(world->cells);
    free(world);
    return NULL;
}

void world_set_monod_kinetics(World* world, const MonodKineticsConfig* config) {
    if (!world || !config) return;

    float uptake_min = utils_clamp_f(config->uptake_min, 0.0f, 1.0f);
    float uptake_max = utils_clamp_f(config->uptake_max, 0.0f, 1.0f);
    if (uptake_max < uptake_min) {
        float tmp = uptake_min;
        uptake_min = uptake_max;
        uptake_max = tmp;
    }

    world->monod.enabled = config->enabled;
    world->monod.half_saturation = config->half_saturation < 0.0f ? 0.0f : config->half_saturation;
    world->monod.uptake_min = uptake_min;
    world->monod.uptake_max = uptake_max;
    world->monod.growth_coupling = utils_clamp_f(config->growth_coupling, 0.0f, 1.0f);
}

MonodKineticsConfig world_get_monod_kinetics(const World* world) {
    MonodKineticsConfig config = {
        .enabled = MONOD_DEFAULT_ENABLED,
        .half_saturation = MONOD_DEFAULT_HALF_SATURATION,
        .uptake_min = MONOD_DEFAULT_UPTAKE_MIN,
        .uptake_max = MONOD_DEFAULT_UPTAKE_MAX,
        .growth_coupling = MONOD_DEFAULT_GROWTH_COUPLING,
    };
    if (!world) return config;

    config.enabled = world->monod.enabled;
    config.half_saturation = world->monod.half_saturation;
    config.uptake_min = world->monod.uptake_min;
    config.uptake_max = world->monod.uptake_max;
    config.growth_coupling = world->monod.growth_coupling;
    return config;
}

void world_destroy(World* world) {
    if (!world) return;
    
    // Free cell_indices for each colony
    if (world->colonies) {
        for (size_t i = 0; i < world->colony_count; i++) {
            if (world->colonies[i].cell_indices) {
                free(world->colonies[i].cell_indices);
            }
        }
        free(world->colonies);
    }
    if (world->cells) free(world->cells);
    if (world->colony_by_id) free(world->colony_by_id);
    if (world->nutrients) free(world->nutrients);
    if (world->toxins) free(world->toxins);
    if (world->signals) free(world->signals);
    if (world->alarm_signals) free(world->alarm_signals);
    if (world->signal_source) free(world->signal_source);
    if (world->alarm_source) free(world->alarm_source);
    if (world->scratch_signals) free(world->scratch_signals);
    if (world->scratch_sources) free(world->scratch_sources);
    free(world);
}

void world_init_random_colonies(World* world, int count) {
    if (!world || count <= 0) return;
    
    for (int i = 0; i < count; i++) {
        Colony colony;
        memset(&colony, 0, sizeof(Colony));
        colony.id = 0;  // Will be assigned by world_add_colony
        generate_scientific_name(colony.name, sizeof(colony.name));
        colony.genome = genome_create_random();
        colony.color = colony.genome.body_color;  // Use genome's body color
        colony.cell_count = 0;
        colony.max_cell_count = 0;
        colony.active = true;
        colony.age = 0;
        colony.parent_id = 0;
        colony.hgt_plasmid_fraction = utils_clamp_f(colony.genome.gene_transfer_rate * 0.25f, 0.0f, 0.35f);
        colony.hgt_is_transconjugant = false;
        
        // Generate unique shape seed for procedural shape generation
        colony.shape_seed = (uint32_t)rand() ^ ((uint32_t)rand() << 16);
        colony.wobble_phase = (float)(rand() % 628) / 100.0f;
        
        uint32_t id = world_add_colony(world, colony);
        
        // Place colony at random position (retry if occupied)
        int x, y;
        int max_tries = 100;
        Cell* cell = NULL;
        for (int try = 0; try < max_tries; try++) {
            x = rand_range(0, world->width - 1);
            y = rand_range(0, world->height - 1);
            cell = world_get_cell(world, x, y);
            if (cell && cell->colony_id == 0) {
                break;
            }
            cell = NULL;
        }
        
        if (cell) {
            cell->colony_id = id;
            cell->age = 0;
            
            // Update cell count
            Colony* col = world_get_colony(world, id);
            if (col) {
                col->cell_count = 1;
                col->max_cell_count = 1;
            }
        } else {
            // Could not find empty cell - deactivate colony
            Colony* col = world_get_colony(world, id);
            if (col) {
                col->active = false;
            }
        }
    }
}

Cell* world_get_cell(World* world, int x, int y) {
    if (!world || x < 0 || x >= world->width || y < 0 || y >= world->height) {
        return NULL;
    }
    return &world->cells[y * world->width + x];
}

Colony* world_get_colony(World* world, uint32_t id) {
    if (!world || id == 0) return NULL;
    
    if (id < world->colony_by_id_capacity) {
        Colony* c = world->colony_by_id[id];
        if (c && c->active) return c;
    }
    return NULL;
}

uint32_t world_add_colony(World* world, Colony colony) {
    if (!world) return 0;
    
    // Check for ID overflow
    uint32_t old_id = atomic_load(&world->next_colony_id);
    if (old_id == UINT32_MAX) {
        return 0;  // Cannot assign more colony IDs
    }
    
    // Expand array if needed
    if (world->colony_count >= world->colony_capacity) {
        size_t new_capacity = world->colony_capacity * 2;
        Colony* new_colonies = (Colony*)realloc(world->colonies, new_capacity * sizeof(Colony));
        if (!new_colonies) return 0;
        // Realloc may move the array; rebuild all lookup pointers
        if (new_colonies != world->colonies) {
            world->colonies = new_colonies;
            for (size_t i = 0; i < world->colony_count; i++) {
                uint32_t cid = world->colonies[i].id;
                if (cid < world->colony_by_id_capacity && world->colony_by_id[cid]) {
                    world->colony_by_id[cid] = &world->colonies[i];
                }
            }
        }
        world->colonies = new_colonies;
        world->colony_capacity = new_capacity;
    }
    
    // Assign new ID using atomic increment
    colony.id = atomic_fetch_add(&world->next_colony_id, 1);
    colony.active = true;
    colony.is_persister = false;
    
    // Initialize cell tracking and centroid
    colony.cell_indices = NULL;
    colony.cell_indices_capacity = 0;
    colony.cell_indices_count = 0;
    colony.centroid_x = 0;
    colony.centroid_y = 0;
    if (colony.parent_id != 0 && colony.hgt_plasmid_fraction <= 0.0f) {
        Colony* parent = world_get_colony(world, colony.parent_id);
        if (parent) {
            colony.hgt_plasmid_fraction = parent->hgt_plasmid_fraction;
            colony.hgt_is_transconjugant = parent->hgt_is_transconjugant;
        }
    }

    if (colony.parent_id == 0 && colony.hgt_plasmid_fraction <= 0.0f && colony.cell_count > 0) {
        colony.hgt_plasmid_fraction = utils_clamp_f(colony.genome.gene_transfer_rate * 0.25f, 0.0f, 0.35f);
        colony.hgt_is_transconjugant = false;
    }

    colony.hgt_plasmid_fraction = utils_clamp_f(colony.hgt_plasmid_fraction, 0.0f, 1.0f);
    colony.hgt_fitness_scale = 1.0f;
    colony.hgt_is_transconjugant = colony.hgt_is_transconjugant && colony.hgt_plasmid_fraction > 0.0f;
    colony.hgt_transfer_events_in = 0;
    colony.hgt_transfer_events_out = 0;
    colony.hgt_plasmid_loss_events = 0;
    
    world->colonies[world->colony_count] = colony;
    
    // Grow lookup table if needed
    if (colony.id >= world->colony_by_id_capacity) {
        size_t new_cap = world->colony_by_id_capacity;
        while (new_cap <= colony.id) new_cap *= 2;
        Colony** new_table = (Colony**)realloc(world->colony_by_id, new_cap * sizeof(Colony*));
        if (!new_table) return 0;
        memset(new_table + world->colony_by_id_capacity, 0,
               (new_cap - world->colony_by_id_capacity) * sizeof(Colony*));
        world->colony_by_id = new_table;
        world->colony_by_id_capacity = new_cap;
    }
    world->colony_by_id[colony.id] = &world->colonies[world->colony_count];
    
    world->colony_count++;
    return colony.id;
}

void world_set_hgt_kinetics(World* world, const HGTKinetics* kinetics) {
    if (!world || !kinetics) return;
    world->hgt_kinetics = *kinetics;
}

void world_reset_hgt_kinetics(World* world) {
    if (!world) return;
    world->hgt_kinetics = DEFAULT_HGT_KINETICS;
}

void world_reset_hgt_metrics(World* world) {
    if (!world) return;
    memset(&world->hgt_metrics, 0, sizeof(world->hgt_metrics));
}

void world_colony_add_cell(World* world, uint32_t colony_id, uint32_t cell_idx) {
    if (!world || colony_id == 0) return;
    
    Colony* colony = world_get_colony(world, colony_id);
    if (!colony) return;
    
    // Expand cell_indices array if needed
    if (colony->cell_indices_count >= colony->cell_indices_capacity) {
        size_t new_cap = colony->cell_indices_capacity == 0 ? 64 : colony->cell_indices_capacity * 2;
        uint32_t* new_indices = (uint32_t*)realloc(colony->cell_indices, new_cap * sizeof(uint32_t));
        if (!new_indices) return;
        colony->cell_indices = new_indices;
        colony->cell_indices_capacity = new_cap;
    }
    
    colony->cell_indices[colony->cell_indices_count++] = cell_idx;
    
    // Update centroid using running average
    // Note: cell_count is already incremented by caller, so use cell_count - 1 for old count
    if (colony->cell_count > 1) {
        int x = cell_idx % world->width;
        int y = cell_idx / world->width;
        float old_count = (float)(colony->cell_count - 1);
        float new_count = (float)colony->cell_count;
        colony->centroid_x = (colony->centroid_x * old_count + (float)x) / new_count;
        colony->centroid_y = (colony->centroid_y * old_count + (float)y) / new_count;
    } else {
        colony->centroid_x = (float)(cell_idx % world->width);
        colony->centroid_y = (float)(cell_idx / world->width);
    }
}

void world_colony_remove_cell(World* world, uint32_t colony_id, uint32_t cell_idx) {
    if (!world || colony_id == 0) return;

    Colony* colony = world_get_colony(world, colony_id);
    if (!colony || colony->cell_count == 0) return;

    // Remove cell_idx from cell_indices array
    for (size_t i = 0; i < colony->cell_indices_count; i++) {
        if (colony->cell_indices[i] == cell_idx) {
            // Shift remaining elements to fill the gap
            for (size_t j = i; j < colony->cell_indices_count - 1; j++) {
                colony->cell_indices[j] = colony->cell_indices[j + 1];
            }
            colony->cell_indices_count--;
            break;
        }
    }

    // Update centroid using running average (remove contribution)
    int x = cell_idx % world->width;
    int y = cell_idx / world->width;
    if (colony->cell_count > 1) {
        float total_x = colony->centroid_x * (float)colony->cell_count;
        float total_y = colony->centroid_y * (float)colony->cell_count;
        colony->centroid_x = (total_x - (float)x) / (float)(colony->cell_count - 1);
        colony->centroid_y = (total_y - (float)y) / (float)(colony->cell_count - 1);
    } else {
        colony->centroid_x = 0;
        colony->centroid_y = 0;
    }
}

void world_remove_colony(World* world, uint32_t id) {
    if (!world || id == 0) return;
    
    // Mark colony as inactive
    Colony* colony = world_get_colony(world, id);
    if (colony) {
        colony->active = false;
        // Clear lookup table entry
        if (id < world->colony_by_id_capacity) {
            world->colony_by_id[id] = NULL;
        }
    }
    
    // Clear all cells belonging to this colony
    if (colony && colony->cell_indices && colony->cell_indices_count > 0) {
        // O(active_cells) - use tracked cell indices
        for (size_t i = 0; i < colony->cell_indices_count; i++) {
            uint32_t cell_idx = colony->cell_indices[i];
            if (cell_idx < (uint32_t)(world->width * world->height)) {
                world->cells[cell_idx].colony_id = 0;
                world->cells[cell_idx].age = 0;
            }
        }
        free(colony->cell_indices);
        colony->cell_indices = NULL;
        colony->cell_indices_capacity = 0;
        colony->cell_indices_count = 0;
    } else {
        // Fallback: O(width*height) scan entire grid
        for (int i = 0; i < world->width * world->height; i++) {
            if (world->cells[i].colony_id == id) {
                world->cells[i].colony_id = 0;
                world->cells[i].age = 0;
            }
        }
    }
}

bool world_set_rd_controls(World* world, const RDSolverControls* controls,
                           char* err_buf, size_t err_buf_size) {
    if (!world || !controls) {
        return false;
    }

    if (!world_validate_rd_field(&controls->nutrients, "nutrients", err_buf, err_buf_size) ||
        !world_validate_rd_field(&controls->toxins, "toxins", err_buf, err_buf_size) ||
        !world_validate_rd_field(&controls->signals, "signals", err_buf, err_buf_size)) {
        return false;
    }

    world->rd_controls = *controls;
    return true;
}

RDSolverControls world_get_rd_controls(const World* world) {
    if (!world) {
        return world_default_rd_controls();
    }
    return world->rd_controls;
}
