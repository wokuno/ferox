#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "../src/shared/protocol.h"

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void fill_sparse(uint16_t* grid, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        grid[i] = (i % 97u == 0u) ? 7u : 0u;
    }
}

static void fill_noisy(uint16_t* grid, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        grid[i] = (uint16_t)((i * 131u) & 0x00FFu);
    }
}

static int benchmark_case(const char* kind, uint16_t* grid, uint32_t size, int repeats) {
    uint64_t ser_ns = 0;
    uint64_t de_ns = 0;
    size_t bytes_total = 0;

    uint16_t* decoded = (uint16_t*)calloc(size, sizeof(uint16_t));
    if (!decoded) return 1;

    for (int i = 0; i < repeats; i++) {
        uint8_t* buf = NULL;
        size_t len = 0;

        uint64_t t0 = now_ns();
        if (protocol_serialize_grid_rle(grid, size, &buf, &len) != 0) {
            free(decoded);
            return 1;
        }
        uint64_t t1 = now_ns();
        if (protocol_deserialize_grid_rle(buf, len, decoded, size) != 0) {
            free(buf);
            free(decoded);
            return 1;
        }
        uint64_t t2 = now_ns();

        ser_ns += (t1 - t0);
        de_ns += (t2 - t1);
        bytes_total += len;

        free(buf);
    }

    size_t mismatches = 0;
    for (uint32_t i = 0; i < size; i++) {
        if (decoded[i] != grid[i]) {
            mismatches++;
            break;
        }
    }
    free(decoded);

    if (mismatches != 0) {
        return 1;
    }

    double avg_bytes = (double)bytes_total / (double)repeats;
    double raw_bytes = (double)size * sizeof(uint16_t);
    double ser_ns_per_cell = (double)ser_ns / (double)(repeats * (int)size);
    double de_ns_per_cell = (double)de_ns / (double)(repeats * (int)size);

    printf("UNIT_PROTOCOL kind=%s size=%u avg_bytes=%.0f ratio=%.3f ser_ns_cell=%.3f de_ns_cell=%.3f\n",
           kind,
           size,
           avg_bytes,
           avg_bytes / raw_bytes,
           ser_ns_per_cell,
           de_ns_per_cell);

    return 0;
}

static int benchmark_chunk_case(const char* kind, uint16_t* grid, uint32_t size, int repeats) {
    uint64_t ser_ns = 0;
    uint64_t de_ns = 0;
    size_t bytes_total = 0;

    uint16_t* decoded = (uint16_t*)calloc(size, sizeof(uint16_t));
    if (!decoded) return 1;

    uint32_t chunk_count = (size + MAX_GRID_CHUNK_CELLS - 1u) / MAX_GRID_CHUNK_CELLS;
    for (int r = 0; r < repeats; r++) {
        memset(decoded, 0, size * sizeof(uint16_t));
        for (uint32_t chunk_idx = 0; chunk_idx < chunk_count; chunk_idx++) {
            uint32_t start_index = chunk_idx * MAX_GRID_CHUNK_CELLS;
            uint32_t cell_count = size - start_index;
            if (cell_count > MAX_GRID_CHUNK_CELLS) {
                cell_count = MAX_GRID_CHUNK_CELLS;
            }

            ProtoWorldDeltaGridChunk chunk;
            proto_world_delta_grid_chunk_init(&chunk);
            chunk.tick = 999;
            chunk.width = size;
            chunk.height = 1;
            chunk.total_cells = size;
            chunk.start_index = start_index;
            chunk.cell_count = cell_count;
            chunk.final_chunk = (chunk_idx + 1u == chunk_count);
            chunk.cells = &grid[start_index];

            uint8_t* buf = NULL;
            size_t len = 0;
            uint64_t t0 = now_ns();
            if (protocol_serialize_world_delta_grid_chunk(&chunk, &buf, &len) != 0) {
                free(decoded);
                return 1;
            }
            uint64_t t1 = now_ns();

            ProtoWorldDeltaGridChunk decoded_chunk;
            proto_world_delta_grid_chunk_init(&decoded_chunk);
            if (protocol_deserialize_world_delta_grid_chunk(buf, len, &decoded_chunk) != 0) {
                free(buf);
                free(decoded);
                return 1;
            }
            uint64_t t2 = now_ns();

            memcpy(&decoded[decoded_chunk.start_index], decoded_chunk.cells,
                   (size_t)decoded_chunk.cell_count * sizeof(uint16_t));
            proto_world_delta_grid_chunk_free(&decoded_chunk);
            free(buf);

            ser_ns += (t1 - t0);
            de_ns += (t2 - t1);
            bytes_total += len;
        }
    }

    size_t mismatches = 0;
    for (uint32_t i = 0; i < size; i++) {
        if (decoded[i] != grid[i]) {
            mismatches++;
            break;
        }
    }
    free(decoded);

    if (mismatches != 0) {
        return 1;
    }

    double avg_bytes = (double)bytes_total / (double)repeats;
    double raw_bytes = (double)size * sizeof(uint16_t);
    double ser_ns_per_cell = (double)ser_ns / (double)(repeats * (int)size);
    double de_ns_per_cell = (double)de_ns / (double)(repeats * (int)size);

    printf("UNIT_PROTOCOL_CHUNK kind=%s size=%u chunks=%u avg_bytes=%.0f ratio=%.3f ser_ns_cell=%.3f de_ns_cell=%.3f\n",
           kind,
           size,
           chunk_count,
           avg_bytes,
           avg_bytes / raw_bytes,
           ser_ns_per_cell,
           de_ns_per_cell);

    return 0;
}

int main(void) {
    const uint32_t size_small = 4096;
    const uint32_t size_large = 65536;
    const uint32_t size_chunked = 262144;
    const int repeats = 30;

    uint16_t* small = (uint16_t*)malloc(size_small * sizeof(uint16_t));
    uint16_t* large = (uint16_t*)malloc(size_large * sizeof(uint16_t));
    uint16_t* chunked = (uint16_t*)malloc(size_chunked * sizeof(uint16_t));
    if (!small || !large || !chunked) {
        free(small);
        free(large);
        free(chunked);
        return 1;
    }

    fill_sparse(small, size_small);
    if (benchmark_case("sparse_small", small, size_small, repeats) != 0) {
        free(small);
        free(large);
        return 1;
    }

    fill_noisy(small, size_small);
    if (benchmark_case("noisy_small", small, size_small, repeats) != 0) {
        free(small);
        free(large);
        return 1;
    }

    fill_sparse(large, size_large);
    if (benchmark_case("sparse_large", large, size_large, repeats) != 0) {
        free(small);
        free(large);
        return 1;
    }

    fill_noisy(large, size_large);
    if (benchmark_case("noisy_large", large, size_large, repeats) != 0) {
        free(small);
        free(large);
        free(chunked);
        return 1;
    }

    fill_noisy(chunked, size_chunked);
    if (benchmark_chunk_case("noisy_chunked", chunked, size_chunked, repeats) != 0) {
        free(small);
        free(large);
        free(chunked);
        return 1;
    }

    free(small);
    free(large);
    free(chunked);
    return 0;
}
