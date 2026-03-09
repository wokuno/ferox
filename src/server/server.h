/**
 * server.h - Server core for bacterial colony simulator
 * Part of Phase 5: Server Implementation
 */

#ifndef SERVER_H
#define SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#include "../shared/network.h"
#include "../shared/protocol.h"
#include "world.h"
#include "threadpool.h"
#include "parallel.h"
#include "atomic_sim.h"

// Default tick rate (10 ticks per second)
#define DEFAULT_WORLD_WIDTH 400
#define DEFAULT_WORLD_HEIGHT 200
#define DEFAULT_INITIAL_COLONY_COUNT 50
#define DEFAULT_TICK_RATE_MS 100

// Client session represents a connected client
typedef struct ClientSession {
    NetSocket* socket;
    uint32_t id;
    bool active;
    uint32_t selected_colony;  // Colony selected for detailed view
    struct ClientSession* next;
} ClientSession;

// Server structure
typedef struct Server {
    NetServer* listener;
    ClientSession* clients;
    int client_count;
    World* world;
    ThreadPool* pool;
    ParallelContext* parallel_ctx;
    AtomicWorld* atomic_world;    // Atomic simulation engine
    int world_width;
    int world_height;
    int default_colonies;
    bool running;
    bool paused;
    int tick_rate_ms;  // Milliseconds between ticks
    float speed_multiplier;
    pthread_mutex_t clients_mutex;
    pthread_t accept_thread;
    pthread_t simulation_thread;
    uint32_t next_client_id;
} Server;

/**
 * Create a new server instance.
 * @param port Port to listen on (0 for auto-assign)
 * @param world_width Width of the world grid
 * @param world_height Height of the world grid
 * @param thread_count Number of threads for the thread pool
 * @return Pointer to the new Server, or NULL on failure
 */
Server* server_create(uint16_t port, int world_width, int world_height, int thread_count);

/**
 * Create an in-process server instance without opening a listening socket.
 * Intended for benchmarks or local snapshot work that does not accept clients.
 * @param world_width Width of the world grid
 * @param world_height Height of the world grid
 * @param thread_count Number of threads for the thread pool
 * @return Pointer to the new Server, or NULL on failure
 */
Server* server_create_headless(int world_width, int world_height, int thread_count);

/**
 * Destroy the server and free all resources.
 * @param server The server to destroy
 */
void server_destroy(Server* server);

/**
 * Run the server's main loop.
 * This function blocks until server_stop is called.
 * @param server The server to run
 */
void server_run(Server* server);

/**
 * Stop the server gracefully.
 * @param server The server to stop
 */
void server_stop(Server* server);

/**
 * Build a protocol snapshot from a world.
 * Inline grid data is included only when the world fits the inline threshold;
 * larger worlds rely on follow-up delta chunks for grid transfer.
 * @param world Source world state
 * @param paused Whether simulation is paused
 * @param speed_multiplier Current playback speed multiplier
 * @param proto_world Output protocol snapshot
 * @return 0 on success, -1 on failure
 */
int server_build_protocol_world_snapshot(const World* world,
                                         bool paused,
                                         float speed_multiplier,
                                         ProtoWorld* proto_world);

/**
 * Broadcast world state to all connected clients.
 * @param server The server
 */
void server_broadcast_world_state(Server* server);

/**
 * Send detailed colony info to a specific client.
 * @param server The server
 * @param client The client session
 * @param colony_id ID of the colony to send info for
 */
void server_send_colony_info(Server* server, ClientSession* client, uint32_t colony_id);

/**
 * Handle a command from a client.
 * @param server The server
 * @param client The client session that sent the command
 * @param cmd The command type
 * @param data Command-specific data (may be NULL)
 */
void server_handle_command(Server* server, ClientSession* client, CommandType cmd, void* data);

/**
 * Add a new client to the server.
 * @param server The server
 * @param socket The client's socket
 * @return Pointer to the new client session, or NULL on failure
 */
ClientSession* server_add_client(Server* server, NetSocket* socket);

/**
 * Remove a client from the server.
 * @param server The server
 * @param client The client session to remove
 */
void server_remove_client(Server* server, ClientSession* client);

/**
 * Process incoming data from all clients.
 * @param server The server
 */
void server_process_clients(Server* server);

/**
 * Get the server's listening port.
 * @param server The server
 * @return The port number
 */
uint16_t server_get_port(Server* server);

#endif // SERVER_H
