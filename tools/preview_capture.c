#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "../src/server/simulation.h"
#include "../src/server/world.h"

static int ensure_dir(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }

    if (mkdir(path, 0755) == 0) {
        return 0;
    }

    return -1;
}

static void choose_color(const World* world, uint32_t colony_id, uint8_t* r, uint8_t* g, uint8_t* b) {
    if (colony_id == 0) {
        *r = 242;
        *g = 241;
        *b = 232;
        return;
    }

    if (colony_id < world->colony_by_id_capacity) {
        Colony* colony = world->colony_by_id[colony_id];
        if (colony && colony->active) {
            *r = colony->color.r;
            *g = colony->color.g;
            *b = colony->color.b;
            return;
        }
    }

    *r = 120;
    *g = 120;
    *b = 120;
}

static int write_frame_ppm(const World* world, const char* path, int scale) {
    const int out_w = world->width * scale;
    const int out_h = world->height * scale;
    FILE* fp = fopen(path, "wb");
    if (!fp) {
        return -1;
    }

    if (fprintf(fp, "P6\n%d %d\n255\n", out_w, out_h) < 0) {
        fclose(fp);
        return -1;
    }

    for (int y = 0; y < world->height; y++) {
        for (int sy = 0; sy < scale; sy++) {
            (void)sy;
            for (int x = 0; x < world->width; x++) {
                uint32_t idx = (uint32_t)(y * world->width + x);
                uint32_t colony_id = world->cells[idx].colony_id;
                uint8_t rgb[3];
                choose_color(world, colony_id, &rgb[0], &rgb[1], &rgb[2]);
                for (int sx = 0; sx < scale; sx++) {
                    (void)sx;
                    if (fwrite(rgb, 1, sizeof(rgb), fp) != sizeof(rgb)) {
                        fclose(fp);
                        return -1;
                    }
                }
            }
        }
    }

    fclose(fp);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <out_dir> [frames] [width] [height] [colonies] [scale]\n", argv[0]);
        return 1;
    }

    const char* out_dir = argv[1];
    const int frames = argc > 2 ? atoi(argv[2]) : 90;
    const int width = argc > 3 ? atoi(argv[3]) : 180;
    const int height = argc > 4 ? atoi(argv[4]) : 100;
    const int colonies = argc > 5 ? atoi(argv[5]) : 24;
    const int scale = argc > 6 ? atoi(argv[6]) : 4;

    if (frames <= 0 || width <= 0 || height <= 0 || colonies <= 0 || scale <= 0) {
        fprintf(stderr, "Invalid arguments\n");
        return 2;
    }

    if (ensure_dir(out_dir) != 0) {
        fprintf(stderr, "Failed to create output dir '%s': %s\n", out_dir, strerror(errno));
        return 3;
    }

    srand(7);
    World* world = world_create(width, height);
    if (!world) {
        fprintf(stderr, "Failed to create world\n");
        return 4;
    }

    world_init_random_colonies(world, colonies);

    char frame_path[1024];
    for (int i = 0; i < frames; i++) {
        if (snprintf(frame_path, sizeof(frame_path), "%s/frame_%04d.ppm", out_dir, i) >= (int)sizeof(frame_path)) {
            world_destroy(world);
            return 5;
        }

        if (write_frame_ppm(world, frame_path, scale) != 0) {
            fprintf(stderr, "Failed to write frame %d\n", i);
            world_destroy(world);
            return 6;
        }

        simulation_tick(world);
    }

    world_destroy(world);
    return 0;
}
