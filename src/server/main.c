/**
 * main.c - Server entry point for bacterial colony simulator
 * Part of Phase 5: Server Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <limits.h>

#include "server.h"
#include "world.h"
#include "atomic_sim.h"

static volatile sig_atomic_t g_shutdown_requested = 0;

// Signal handler for graceful shutdown
static void signal_handler(int sig) {
    (void)sig;
    g_shutdown_requested = 1;
}

static void* server_thread_main(void* arg) {
    Server* server = (Server*)arg;
    server_run(server);
    return NULL;
}

// Print usage information
static void print_usage(const char* program) {
    printf("Usage: %s [options]\n", program);
    printf("\nOptions:\n");
    printf("  -p, --port <port>        Port to listen on (default: 8080, 0 for auto)\n");
    printf("  -w, --width <width>      World width (default: 100)\n");
    printf("  -H, --height <height>    World height (default: 100)\n");
    printf("  -t, --threads <count>    Thread pool size (default: 4)\n");
    printf("  -c, --colonies <count>   Initial colony count (default: 5)\n");
    printf("  -r, --rate <ms>          Tick rate in milliseconds (default: 100)\n");
    printf("      --nutrient-diffusion <v>  Nutrient diffusion [0.0, 0.25] (default: %.3f)\n", RD_DEFAULT_NUTRIENT_DIFFUSION);
    printf("      --nutrient-decay <v>      Nutrient decay [0.0, 1.0] (default: %.3f)\n", RD_DEFAULT_NUTRIENT_DECAY);
    printf("      --toxin-diffusion <v>     Toxin diffusion [0.0, 0.25] (default: %.3f)\n", RD_DEFAULT_TOXIN_DIFFUSION);
    printf("      --toxin-decay <v>         Toxin decay [0.0, 1.0] (default: %.3f)\n", RD_DEFAULT_TOXIN_DECAY);
    printf("      --signal-diffusion <v>    Signal diffusion [0.0, 0.25] (default: %.3f)\n", RD_DEFAULT_SIGNAL_DIFFUSION);
    printf("      --signal-decay <v>        Signal decay [0.0, 1.0] (default: %.3f)\n", RD_DEFAULT_SIGNAL_DECAY);
    printf("  -h, --help               Show this help message\n");
}

static bool parse_int_arg(const char* arg, int min_value, int max_value, int* out_value) {
    if (!arg || !out_value) return false;

    errno = 0;
    char* end = NULL;
    long parsed = strtol(arg, &end, 10);
    if (errno != 0 || end == arg || *end != '\0') {
        return false;
    }
    if (parsed < min_value || parsed > max_value) {
        return false;
    }

    *out_value = (int)parsed;
    return true;
}

static bool parse_port_arg(const char* arg, uint16_t* out_port) {
    int parsed = 0;
    if (!parse_int_arg(arg, 0, 65535, &parsed)) {
        return false;
    }
    *out_port = (uint16_t)parsed;
    return true;
}

static bool parse_float_arg(const char* arg, float min_value, float max_value, float* out_value) {
    if (!arg || !out_value) return false;

    errno = 0;
    char* end = NULL;
    float parsed = strtof(arg, &end);
    if (errno != 0 || end == arg || *end != '\0') {
        return false;
    }
    if (parsed < min_value || parsed > max_value) {
        return false;
    }

    *out_value = parsed;
    return true;
}

int main(int argc, char* argv[]) {
    // Default configuration
    uint16_t port = 8080;
    int world_width = 200;
    int world_height = 100;
    int thread_count = 4;
    int initial_colonies = 20;
    int tick_rate_ms = 50;  // Faster default (was 100ms)

    RDSolverControls rd_controls = {
        .nutrients = {RD_DEFAULT_NUTRIENT_DIFFUSION, RD_DEFAULT_NUTRIENT_DECAY},
        .toxins = {RD_DEFAULT_TOXIN_DIFFUSION, RD_DEFAULT_TOXIN_DECAY},
        .signals = {RD_DEFAULT_SIGNAL_DIFFUSION, RD_DEFAULT_SIGNAL_DECAY},
    };
    
    // Long options
    static struct option long_options[] = {
        {"port",     required_argument, 0, 'p'},
        {"width",    required_argument, 0, 'w'},
        {"height",   required_argument, 0, 'H'},
        {"threads",  required_argument, 0, 't'},
        {"colonies", required_argument, 0, 'c'},
        {"rate",     required_argument, 0, 'r'},
        {"nutrient-diffusion", required_argument, 0, 1000},
        {"nutrient-decay", required_argument, 0, 1001},
        {"toxin-diffusion", required_argument, 0, 1002},
        {"toxin-decay", required_argument, 0, 1003},
        {"signal-diffusion", required_argument, 0, 1004},
        {"signal-decay", required_argument, 0, 1005},
        {"help",     no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    // Parse command line arguments
    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "p:w:H:t:c:r:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'p':
                if (!parse_port_arg(optarg, &port)) {
                    fprintf(stderr, "Error: Invalid port '%s'\n", optarg);
                    return 1;
                }
                break;
            case 'w':
                if (!parse_int_arg(optarg, 1, INT_MAX, &world_width)) {
                    fprintf(stderr, "Error: Invalid world width '%s'\n", optarg);
                    return 1;
                }
                break;
            case 'H':
                if (!parse_int_arg(optarg, 1, INT_MAX, &world_height)) {
                    fprintf(stderr, "Error: Invalid world height '%s'\n", optarg);
                    return 1;
                }
                break;
            case 't':
                if (!parse_int_arg(optarg, 1, INT_MAX, &thread_count)) {
                    fprintf(stderr, "Error: Invalid thread count '%s'\n", optarg);
                    return 1;
                }
                break;
            case 'c':
                if (!parse_int_arg(optarg, 0, INT_MAX, &initial_colonies)) {
                    fprintf(stderr, "Error: Invalid colony count '%s'\n", optarg);
                    return 1;
                }
                break;
            case 'r':
                if (!parse_int_arg(optarg, 1, INT_MAX, &tick_rate_ms)) {
                    fprintf(stderr, "Error: Invalid tick rate '%s'\n", optarg);
                    return 1;
                }
                break;
            case 1000:
                if (!parse_float_arg(optarg, 0.0f, RD_FIELD_MAX_DIFFUSION, &rd_controls.nutrients.diffusion)) {
                    fprintf(stderr, "Error: Invalid nutrient diffusion '%s'\n", optarg);
                    return 1;
                }
                break;
            case 1001:
                if (!parse_float_arg(optarg, 0.0f, 1.0f, &rd_controls.nutrients.decay)) {
                    fprintf(stderr, "Error: Invalid nutrient decay '%s'\n", optarg);
                    return 1;
                }
                break;
            case 1002:
                if (!parse_float_arg(optarg, 0.0f, RD_FIELD_MAX_DIFFUSION, &rd_controls.toxins.diffusion)) {
                    fprintf(stderr, "Error: Invalid toxin diffusion '%s'\n", optarg);
                    return 1;
                }
                break;
            case 1003:
                if (!parse_float_arg(optarg, 0.0f, 1.0f, &rd_controls.toxins.decay)) {
                    fprintf(stderr, "Error: Invalid toxin decay '%s'\n", optarg);
                    return 1;
                }
                break;
            case 1004:
                if (!parse_float_arg(optarg, 0.0f, RD_FIELD_MAX_DIFFUSION, &rd_controls.signals.diffusion)) {
                    fprintf(stderr, "Error: Invalid signal diffusion '%s'\n", optarg);
                    return 1;
                }
                break;
            case 1005:
                if (!parse_float_arg(optarg, 0.0f, 1.0f, &rd_controls.signals.decay)) {
                    fprintf(stderr, "Error: Invalid signal decay '%s'\n", optarg);
                    return 1;
                }
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    // Print configuration
    printf("Bacterial Colony Simulator Server\n");
    printf("==================================\n");
    printf("Port:            %u%s\n", port, port == 0 ? " (auto)" : "");
    printf("World size:      %dx%d\n", world_width, world_height);
    printf("Thread count:    %d\n", thread_count);
    printf("Initial colonies: %d\n", initial_colonies);
    printf("Tick rate:       %d ms\n", tick_rate_ms);
    printf("RD nutrients:    diffusion=%.3f decay=%.3f\n", rd_controls.nutrients.diffusion, rd_controls.nutrients.decay);
    printf("RD toxins:       diffusion=%.3f decay=%.3f\n", rd_controls.toxins.diffusion, rd_controls.toxins.decay);
    printf("RD signals:      diffusion=%.3f decay=%.3f\n", rd_controls.signals.diffusion, rd_controls.signals.decay);
    printf("\n");
    
    // Create server
    printf("Creating server...\n");
    Server* server = server_create(port, world_width, world_height, thread_count);
    if (!server) {
        fprintf(stderr, "Error: Failed to create server\n");
        return 1;
    }

    char rd_error[256];
    if (!world_set_rd_controls(server->world, &rd_controls, rd_error, sizeof(rd_error))) {
        fprintf(stderr, "Error: Invalid reaction-diffusion controls: %s\n", rd_error[0] ? rd_error : "validation failed");
        server_destroy(server);
        return 1;
    }
    
    // Set tick rate
    server->tick_rate_ms = tick_rate_ms;
    
    // Initialize colonies
    if (initial_colonies > 0) {
        printf("Initializing %d random colonies...\n", initial_colonies);
        world_init_random_colonies(server->world, initial_colonies);
        
        // Re-sync atomic world after adding colonies
        atomic_world_sync_from_world(server->atomic_world);
    }
    
    // Setup signal handlers
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    // Print listening info
    printf("Server listening on port %u\n", server_get_port(server));
    printf("Press Ctrl+C to stop\n\n");
    
    // Run server in a dedicated thread and handle shutdown in main thread
    pthread_t server_thread;
    if (pthread_create(&server_thread, NULL, server_thread_main, server) != 0) {
        fprintf(stderr, "Error: Failed to start server thread\n");
        server_destroy(server);
        return 1;
    }

    while (!g_shutdown_requested) {
        pause();
    }

    printf("\nReceived shutdown signal, stopping server...\n");
    server_stop(server);
    pthread_join(server_thread, NULL);
    
    // Cleanup
    printf("Shutting down...\n");
    server_destroy(server);
    
    printf("Server stopped.\n");
    return 0;
}
