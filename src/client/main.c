#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <math.h>

#include "client.h"
#include "input.h"
#include "renderer.h"

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 7890

static Client* g_client = NULL;
static volatile sig_atomic_t g_running = 1;

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
    if (g_client) {
        g_client->running = false;
    }
}

static void cleanup(void) {
    // Restore terminal
    input_cleanup();
    
    // Show cursor
    printf(ANSI_SHOW_CURSOR);
    printf(ANSI_RESET);
    fflush(stdout);
    
    // Cleanup client
    if (g_client) {
        client_destroy(g_client);
        g_client = NULL;
    }
}

static void print_usage(const char* program) {
    fprintf(stderr, "Usage: %s [options]\n", program);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -h, --host HOST    Server host (default: %s)\n", DEFAULT_HOST);
    fprintf(stderr, "  -p, --port PORT    Server port (default: %d)\n", DEFAULT_PORT);
    fprintf(stderr, "  --help             Show this help message\n");
    fprintf(stderr, "\nControls:\n");
    fprintf(stderr, "  Q            Quit\n");
    fprintf(stderr, "  P / Space    Pause/Resume simulation\n");
    fprintf(stderr, "  +/-          Speed up/slow down\n");
    fprintf(stderr, "  Arrow keys   Scroll viewport\n");
    fprintf(stderr, "  WASD         Alternative scroll keys\n");
    fprintf(stderr, "  TAB          Select next colony\n");
    fprintf(stderr, "  ESC          Deselect colony\n");
    fprintf(stderr, "  R            Reset simulation\n");
}

static void init_colony_wobble(proto_colony* colony) {
    // Generate unique shape seed for procedural shape generation
    colony->shape_seed = (uint32_t)rand() ^ ((uint32_t)rand() << 16);
    colony->wobble_phase = (float)(rand() % 628) / 100.0f;  // Random phase 0-2*PI
}

static void run_demo_mode(void) {
    // Demo mode - run without server connection
    printf("Running in demo mode (no server connection)\n");
    printf("Press any key to start, Q to quit\n");
    
    input_init();
    
    // Wait for keypress
    while (input_poll() == INPUT_NONE) {
        usleep(50000);
    }
    
    g_client = client_create();
    if (!g_client) {
        fprintf(stderr, "Failed to create client\n");
        return;
    }
    
    // Create demo world data - larger world with more colonies
    g_client->local_world.width = 400;
    g_client->local_world.height = 200;
    g_client->local_world.tick = 0;
    g_client->local_world.paused = false;
    g_client->local_world.speed_multiplier = 1.0f;
    g_client->local_world.colony_count = 30;
    
    // Demo colonies - spread across larger world
    proto_colony demo_colonies[] = {
        {1, "Bacillus feroxii", 40.0f, 30.0f, 8.0f, 150, 150, 0.5f, 255, 100, 100, true, 0, 0.0f},
        {2, "Streptococcus viridis", 120.0f, 50.0f, 12.0f, 300, 300, 0.7f, 100, 255, 100, true, 0, 0.0f},
        {3, "Lactobacillus azurae", 70.0f, 100.0f, 6.0f, 80, 80, 0.3f, 100, 100, 255, true, 0, 0.0f},
        {4, "Clostridium aureum", 180.0f, 70.0f, 10.0f, 200, 200, 0.6f, 255, 255, 100, true, 0, 0.0f},
        {5, "Enterococcus roseus", 240.0f, 120.0f, 5.0f, 50, 50, 0.2f, 255, 100, 255, true, 0, 0.0f},
        {6, "Pseudomonas cyanea", 300.0f, 40.0f, 9.0f, 180, 180, 0.55f, 50, 200, 255, true, 0, 0.0f},
        {7, "Staphylococcus amber", 340.0f, 140.0f, 7.0f, 120, 120, 0.4f, 255, 180, 50, true, 0, 0.0f},
        {8, "Escherichia verdant", 90.0f, 160.0f, 11.0f, 250, 250, 0.65f, 50, 255, 50, true, 0, 0.0f},
        {9, "Salmonella crimson", 160.0f, 130.0f, 6.0f, 90, 90, 0.35f, 200, 50, 50, true, 0, 0.0f},
        {10, "Klebsiella indigo", 260.0f, 90.0f, 8.0f, 160, 160, 0.5f, 100, 50, 200, true, 0, 0.0f},
        {11, "Proteus teal", 30.0f, 110.0f, 5.0f, 60, 60, 0.25f, 50, 200, 180, true, 0, 0.0f},
        {12, "Serratia coral", 370.0f, 80.0f, 7.0f, 110, 110, 0.45f, 255, 130, 100, true, 0, 0.0f},
        {13, "Acinetobacter olive", 200.0f, 170.0f, 9.0f, 170, 170, 0.5f, 150, 180, 50, true, 0, 0.0f},
        {14, "Moraxella violet", 110.0f, 20.0f, 6.0f, 85, 85, 0.3f, 180, 100, 220, true, 0, 0.0f},
        {15, "Neisseria gold", 280.0f, 150.0f, 10.0f, 210, 210, 0.6f, 255, 215, 0, true, 0, 0.0f},
        {16, "Vibrio azure", 50.0f, 180.0f, 7.0f, 130, 130, 0.45f, 80, 150, 255, true, 0, 0.0f},
        {17, "Rickettsia ruby", 320.0f, 25.0f, 8.0f, 140, 140, 0.5f, 220, 50, 80, true, 0, 0.0f},
        {18, "Bordetella mint", 150.0f, 180.0f, 6.0f, 100, 100, 0.35f, 100, 220, 150, true, 0, 0.0f},
        {19, "Legionella peach", 380.0f, 170.0f, 9.0f, 190, 190, 0.55f, 255, 180, 150, true, 0, 0.0f},
        {20, "Brucella slate", 220.0f, 35.0f, 7.0f, 115, 115, 0.4f, 100, 120, 140, true, 0, 0.0f},
        {21, "Francisella lime", 60.0f, 60.0f, 5.0f, 70, 70, 0.25f, 180, 255, 100, true, 0, 0.0f},
        {22, "Coxiella bronze", 350.0f, 100.0f, 8.0f, 155, 155, 0.5f, 200, 150, 80, true, 0, 0.0f},
        {23, "Bartonella plum", 130.0f, 85.0f, 6.0f, 95, 95, 0.35f, 150, 80, 180, true, 0, 0.0f},
        {24, "Ehrlichia sage", 270.0f, 185.0f, 10.0f, 200, 200, 0.6f, 140, 180, 140, true, 0, 0.0f},
        {25, "Anaplasma rose", 15.0f, 150.0f, 7.0f, 125, 125, 0.45f, 255, 150, 180, true, 0, 0.0f},
        {26, "Orientia ocean", 190.0f, 15.0f, 8.0f, 145, 145, 0.5f, 50, 120, 200, true, 0, 0.0f},
        {27, "Neorickettsia sun", 310.0f, 60.0f, 6.0f, 85, 85, 0.3f, 255, 220, 100, true, 0, 0.0f},
        {28, "Wolbachia forest", 85.0f, 140.0f, 9.0f, 175, 175, 0.55f, 60, 140, 80, true, 0, 0.0f},
        {29, "Chlamydia sky", 360.0f, 130.0f, 7.0f, 120, 120, 0.4f, 135, 200, 235, true, 0, 0.0f},
        {30, "Spiroplasma ember", 240.0f, 55.0f, 8.0f, 150, 150, 0.5f, 255, 100, 50, true, 0, 0.0f},
    };
    memcpy(g_client->local_world.colonies, demo_colonies, sizeof(demo_colonies));
    
    // Initialize wobble factors for each colony
    for (uint32_t i = 0; i < g_client->local_world.colony_count; i++) {
        init_colony_wobble(&g_client->local_world.colonies[i]);
    }
    
    // Run client loop (demo mode)
    g_client->running = true;
    
    uint32_t frame = 0;
    while (g_client->running && g_running) {
        // Process input
        InputAction action = input_poll();
        
        switch (action) {
            case INPUT_QUIT:
                g_client->running = false;
                break;
            case INPUT_PAUSE:
                g_client->local_world.paused = !g_client->local_world.paused;
                break;
            case INPUT_SPEED_UP:
                g_client->local_world.speed_multiplier *= 1.5f;
                if (g_client->local_world.speed_multiplier > 100.0f)
                    g_client->local_world.speed_multiplier = 100.0f;
                break;
            case INPUT_SLOW_DOWN:
                g_client->local_world.speed_multiplier /= 1.5f;
                if (g_client->local_world.speed_multiplier < 0.1f)
                    g_client->local_world.speed_multiplier = 0.1f;
                break;
            case INPUT_SCROLL_UP:
                renderer_scroll(g_client->renderer, 0, -5);
                break;
            case INPUT_SCROLL_DOWN:
                renderer_scroll(g_client->renderer, 0, 5);
                break;
            case INPUT_SCROLL_LEFT:
                renderer_scroll(g_client->renderer, -5, 0);
                break;
            case INPUT_SCROLL_RIGHT:
                renderer_scroll(g_client->renderer, 5, 0);
                break;
            case INPUT_SELECT:
                client_select_next_colony(g_client);
                break;
            case INPUT_DESELECT:
                client_deselect_colony(g_client);
                break;
            case INPUT_RESET:
                g_client->local_world.tick = 0;
                break;
            default:
                break;
        }
        
        // Update demo world (simple growth simulation)
        if (!g_client->local_world.paused) {
            frame++;
            if (frame % 10 == 0) {
                g_client->local_world.tick++;
                
                // Grow colonies and update wobble
                for (uint32_t i = 0; i < g_client->local_world.colony_count; i++) {
                    proto_colony* c = &g_client->local_world.colonies[i];
                    if (c->alive) {
                        c->radius += c->growth_rate * 0.05f * g_client->local_world.speed_multiplier;
                        c->population = (uint32_t)(c->radius * c->radius * 3.14159f);
                        
                        // Track max population
                        if (c->population > c->max_population) {
                            c->max_population = c->population;
                        }
                        
                        // Cap radius
                        if (c->radius > 30.0f) c->radius = 30.0f;
                        
                        // Animate wobble phase for organic movement
                        c->wobble_phase += 0.05f * g_client->local_world.speed_multiplier;
                        if (c->wobble_phase > 6.28318f) c->wobble_phase -= 6.28318f;
                        
                        // Note: shape_seed is NOT mutated here - that causes jarring visual jumps
                        // Shape evolution happens naturally through wobble_phase animation
                    }
                }
            }
        }
        
        // Render
        renderer_clear(g_client->renderer);
        g_client->renderer->selected_colony = g_client->selected_colony;
        
        renderer_draw_border(g_client->renderer, 
                            (int)g_client->local_world.width,
                            (int)g_client->local_world.height);
        renderer_draw_world(g_client->renderer, &g_client->local_world);
        
        const proto_colony* selected = client_get_selected_colony(g_client);
        renderer_draw_colony_info(g_client->renderer, selected);
        
        int alive = 0;
        for (uint32_t i = 0; i < g_client->local_world.colony_count; i++) {
            if (g_client->local_world.colonies[i].alive) alive++;
        }
        
        renderer_draw_status(g_client->renderer,
                            g_client->local_world.tick,
                            alive,
                            g_client->local_world.paused,
                            g_client->local_world.speed_multiplier);
        
        renderer_present(g_client->renderer);
        
        usleep(33333);  // ~30 FPS
    }
}

int main(int argc, char* argv[]) {
    const char* host = DEFAULT_HOST;
    uint16_t port = DEFAULT_PORT;
    bool demo_mode = false;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--host") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: %s requires an argument\n", argv[i]);
                return 1;
            }
            host = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: %s requires an argument\n", argv[i]);
                return 1;
            }
            port = (uint16_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--demo") == 0) {
            demo_mode = true;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Register cleanup
    atexit(cleanup);
    
    if (demo_mode) {
        run_demo_mode();
        return 0;
    }
    
    // Create client
    g_client = client_create();
    if (!g_client) {
        fprintf(stderr, "Failed to create client\n");
        return 1;
    }
    
    printf("Connecting to %s:%d...\n", host, port);
    
    // Try to connect
    if (!client_connect(g_client, host, port)) {
        fprintf(stderr, "Failed to connect to server at %s:%d\n", host, port);
        fprintf(stderr, "Run with --demo for demo mode\n");
        return 1;
    }
    
    printf("Connected! Starting client...\n");
    usleep(500000);  // Brief pause to show connection message
    
    // Initialize input
    input_init();
    
    // Run client
    client_run(g_client);
    
    printf("\nDisconnecting...\n");
    
    return 0;
}
