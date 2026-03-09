#ifndef NETWORK_H
#define NETWORK_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Socket wrapper for cross-platform compatibility
typedef struct NetSocket {
    int fd;
    bool connected;
    char address[64];
    uint16_t port;
} NetSocket;

// Server socket
typedef struct NetServer {
    int fd;
    uint16_t port;
    bool listening;
} NetServer;

// Server functions
NetServer* net_server_create(uint16_t port);
void net_server_destroy(NetServer* server);
NetSocket* net_server_accept(NetServer* server);  // Blocking accept

// Client functions
NetSocket* net_client_connect(const char* host, uint16_t port);
void net_socket_close(NetSocket* socket);

// Data transfer
int net_send(NetSocket* socket, const uint8_t* data, size_t len);
int net_recv(NetSocket* socket, uint8_t* buffer, size_t max_len);

// Non-blocking check if data available
bool net_has_data(NetSocket* socket);

// Set socket options
void net_set_nonblocking(NetSocket* socket, bool nonblocking);
void net_set_nodelay(NetSocket* socket, bool nodelay);

#endif // NETWORK_H
