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

#include "server.h"
#include "world.h"
#include "atomic_sim.h"
#include "hardware_profile.h"

// Global server pointer for signal handler
static Server* g_server = NULL;

// Signal handler for graceful shutdown
static void signal_handler(int sig) {
    (void)sig;
    printf("\nReceived shutdown signal, stopping server...\n");
    if (g_server) {
        server_stop(g_server);
    }
}

// Print usage information
static void print_usage(const char* program) {
    printf("Usage: %s [options]\n", program);
    printf("\nOptions:\n");
    printf("  -p, --port <port>        Port to listen on (default: 8080, 0 for auto)\n");
    printf("  -w, --width <width>      World width (default: %d)\n", DEFAULT_WORLD_WIDTH);
    printf("  -H, --height <height>    World height (default: %d)\n", DEFAULT_WORLD_HEIGHT);
    printf("  -t, --threads <count>    Thread pool size (default: detected logical CPUs)\n");
    printf("  -c, --colonies <count>   Initial colony count (default: %d)\n", DEFAULT_INITIAL_COLONY_COUNT);
    printf("  -r, --rate <ms>          Tick rate in milliseconds (default: %d)\n", DEFAULT_TICK_RATE_MS);
    printf("  -a, --accelerator <id>   Accelerator target: auto, cpu, apple, amd\n");
    printf("      --print-hardware     Print detected hardware profile and exit\n");
    printf("  -h, --help               Show this help message\n");
}

int main(int argc, char* argv[]) {
    // Default configuration
    uint16_t port = 8080;
    int world_width = DEFAULT_WORLD_WIDTH;
    int world_height = DEFAULT_WORLD_HEIGHT;
    int thread_count = 0;
    int initial_colonies = DEFAULT_INITIAL_COLONY_COUNT;
    int tick_rate_ms = DEFAULT_TICK_RATE_MS;
    bool thread_count_overridden = false;
    bool accelerator_overridden = false;
    bool print_hardware_only = false;
    FeroxAcceleratorPreference accelerator_pref = FEROX_ACCELERATOR_PREFERENCE_AUTO;
    
    // Long options
    static struct option long_options[] = {
        {"port",     required_argument, 0, 'p'},
        {"width",    required_argument, 0, 'w'},
        {"height",   required_argument, 0, 'H'},
        {"threads",  required_argument, 0, 't'},
        {"colonies", required_argument, 0, 'c'},
        {"rate",     required_argument, 0, 'r'},
        {"accelerator", required_argument, 0, 'a'},
        {"print-hardware", no_argument, 0, 1000},
        {"help",     no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    // Parse command line arguments
    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "p:w:H:t:c:r:a:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'p':
                port = (uint16_t)atoi(optarg);
                break;
            case 'w':
                world_width = atoi(optarg);
                if (world_width <= 0) {
                    fprintf(stderr, "Error: Invalid world width\n");
                    return 1;
                }
                break;
            case 'H':
                world_height = atoi(optarg);
                if (world_height <= 0) {
                    fprintf(stderr, "Error: Invalid world height\n");
                    return 1;
                }
                break;
            case 't':
                thread_count = atoi(optarg);
                if (thread_count <= 0) {
                    fprintf(stderr, "Error: Invalid thread count\n");
                    return 1;
                }
                thread_count_overridden = true;
                break;
            case 'c':
                initial_colonies = atoi(optarg);
                if (initial_colonies < 0) {
                    fprintf(stderr, "Error: Invalid colony count\n");
                    return 1;
                }
                break;
            case 'r':
                tick_rate_ms = atoi(optarg);
                if (tick_rate_ms <= 0) {
                    fprintf(stderr, "Error: Invalid tick rate\n");
                    return 1;
                }
                break;
            case 'a':
                if (!ferox_accelerator_preference_from_string(optarg, &accelerator_pref)) {
                    fprintf(stderr, "Error: Invalid accelerator target '%s'\n", optarg);
                    return 1;
                }
                accelerator_overridden = true;
                break;
            case 1000:
                print_hardware_only = true;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    if (!accelerator_overridden) {
        const char* env_accel = getenv("FEROX_ACCELERATOR");
        if (env_accel && *env_accel) {
            if (!ferox_accelerator_preference_from_string(env_accel, &accelerator_pref)) {
                fprintf(stderr, "Warning: Ignoring invalid FEROX_ACCELERATOR=%s\n", env_accel);
                accelerator_pref = FEROX_ACCELERATOR_PREFERENCE_AUTO;
            }
        }
    }

    FeroxHardwareInfo hardware;
    FeroxRuntimeTuning tuning;
    ferox_detect_hardware(&hardware);
    ferox_runtime_tuning_init(&hardware, accelerator_pref, &tuning);

    if (!thread_count_overridden) {
        thread_count = tuning.recommended_threads;
    }

    if (print_hardware_only) {
        ferox_print_hardware_report(stdout, &hardware, &tuning);
        return 0;
    }

    ferox_apply_runtime_tuning_env(&tuning);
    
    // Print configuration
    printf("Bacterial Colony Simulator Server\n");
    printf("==================================\n");
    printf("Host OS:         %s\n", hardware.os_name);
    printf("Host arch:       %s\n", hardware.arch_name);
    printf("CPU / GPU:       %s / %s\n", hardware.cpu_vendor, hardware.gpu_vendor);
    printf("Accelerator:     %s request -> %s target\n",
           ferox_accelerator_preference_name(accelerator_pref),
           ferox_accelerator_backend_name(tuning.selected));
    printf("Compute GPU:     %s\n", hardware.has_compute_gpu ? "detected" : "not detected");
    printf("Port:            %u%s\n", port, port == 0 ? " (auto)" : "");
    printf("World size:      %dx%d\n", world_width, world_height);
    printf("Thread count:    %d\n", thread_count);
    printf("Initial colonies: %d\n", initial_colonies);
    printf("Tick rate:       %d ms\n", tick_rate_ms);
    printf("Thread profile:  %s\n", tuning.threadpool_profile);
    printf("Atomic tuning:   serial=%d frontier_dense=%d%%\n",
           tuning.atomic_serial_interval,
           tuning.atomic_frontier_dense_pct);
    printf("Tuning reason:   %s\n", tuning.reason);
    printf("\n");
    
    // Create server
    printf("Creating server...\n");
    Server* server = server_create(port, world_width, world_height, thread_count);
    if (!server) {
        fprintf(stderr, "Error: Failed to create server\n");
        return 1;
    }
    
    // Set tick rate
    server->tick_rate_ms = tick_rate_ms;
    server->default_colonies = initial_colonies;
    
    // Initialize colonies
    if (initial_colonies > 0) {
        printf("Initializing %d random colonies...\n", initial_colonies);
        world_init_random_colonies(server->world, initial_colonies);
        
        // Re-sync atomic world after adding colonies
        atomic_world_sync_from_world(server->atomic_world);
    }
    
    // Set global server for signal handler
    g_server = server;
    
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
    
    // Run server (blocking)
    server_run(server);
    
    // Cleanup
    printf("Shutting down...\n");
    server_destroy(server);
    g_server = NULL;
    
    printf("Server stopped.\n");
    return 0;
}
