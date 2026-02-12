/**
 * Ferox GUI Client - Bacterial Colony Simulator
 * 
 * A graphical client for the Ferox bacterial colony simulator using SDL2.
 * 
 * Controls:
 *   Space      - Pause/Resume simulation
 *   +/-        - Speed up/slow down
 *   Tab/N/P    - Cycle through colonies
 *   D          - Deselect colony
 *   R          - Reset simulation
 *   G          - Toggle grid overlay
 *   I          - Toggle info panel
 *   Z/Shift+Z  - Zoom in/out
 *   WASD/Arrows- Pan view
 *   Mouse Wheel- Zoom at cursor
 *   Right-drag - Pan view
 *   Left-click - Select colony
 *   Q/Escape   - Quit
 */

#include "gui_client.h"
#include "gui_input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 7777

static void print_usage(const char* program) {
    printf("Ferox GUI Client - Bacterial Colony Simulator\n\n");
    printf("Usage: %s [options]\n\n", program);
    printf("Options:\n");
    printf("  -h, --host <addr>   Server address (default: %s)\n", DEFAULT_HOST);
    printf("  -p, --port <port>   Server port (default: %d)\n", DEFAULT_PORT);
    printf("  --help              Show this help message\n\n");
    printf("Controls:\n");
    printf("  Space        Pause/Resume simulation\n");
    printf("  +/-          Speed up/slow down\n");
    printf("  Tab/N/P      Cycle through colonies\n");
    printf("  D            Deselect colony\n");
    printf("  R            Reset simulation\n");
    printf("  G            Toggle grid overlay\n");
    printf("  I            Toggle info panel\n");
    printf("  Z/Shift+Z    Zoom in/out\n");
    printf("  WASD/Arrows  Pan view\n");
    printf("  Mouse Wheel  Zoom at cursor\n");
    printf("  Right-drag   Pan view\n");
    printf("  Left-click   Select colony\n");
    printf("  Q/Escape     Quit\n");
}

int main(int argc, char* argv[]) {
    const char* host = DEFAULT_HOST;
    uint16_t port = DEFAULT_PORT;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if ((strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--host") == 0) && i + 1 < argc) {
            host = argv[++i];
        } else if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) && i + 1 < argc) {
            port = (uint16_t)atoi(argv[++i]);
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    printf("Ferox GUI Client\n");
    printf("================\n\n");
    
    // Initialize input system
    gui_input_init();
    
    // Create client
    GuiClient* client = gui_client_create();
    if (!client) {
        fprintf(stderr, "Failed to create GUI client\n");
        return 1;
    }
    
    printf("Connecting to %s:%d...\n", host, port);
    
    // Connect to server
    if (!gui_client_connect(client, host, port)) {
        fprintf(stderr, "Failed to connect to server at %s:%d\n", host, port);
        fprintf(stderr, "Make sure the server is running: ./ferox_server\n");
        gui_client_destroy(client);
        return 1;
    }
    
    printf("Connected! Starting GUI...\n\n");
    
    // Run main loop
    gui_client_run(client);
    
    // Cleanup
    gui_client_destroy(client);
    
    printf("\nGoodbye!\n");
    return 0;
}
