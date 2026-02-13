/**
 * server.h - Server core for bacterial colony simulator
 * Part of Phase 5: Server Implementation
 */

#ifndef SERVER_H
#define SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <pthread.h>

#include "../shared/network.h"
#include "../shared/protocol.h"
#include "world.h"
#include "threadpool.h"
#include "parallel.h"
#include "atomic_sim.h"

// Default tick rate (10 ticks per second)
#define DEFAULT_TICK_RATE_MS 100

// Client session represents a connected client
typedef struct client_session {
    net_socket* socket;
    uint32_t id;
    bool active;
    uint32_t selected_colony;  // Colony selected for detailed view
    struct client_session* next;
} client_session;

// Server structure
typedef struct Server {
    net_server* listener;
    client_session* clients;
    int client_count;
    World* world;
    ThreadPool* pool;
    ParallelContext* parallel_ctx;
    AtomicWorld* atomic_world;    // Atomic simulation engine
    _Atomic bool running;
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
void server_send_colony_info(Server* server, client_session* client, uint32_t colony_id);
void server_handle_command(Server* server, client_session* client, CommandType cmd, void* data);
client_session* server_add_client(Server* server, net_socket* socket);
void server_remove_client(Server* server, client_session* client);

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
