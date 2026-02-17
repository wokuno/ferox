#ifndef NETWORK_H
#define NETWORK_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Socket wrapper for cross-platform compatibility
typedef struct net_socket {
    int fd;
    bool connected;
    char address[64];
    uint16_t port;
} net_socket;

// Server socket
typedef struct net_server {
    int fd;
    uint16_t port;
    bool listening;
} net_server;

// Server functions
net_server* net_server_create(uint16_t port);
void net_server_destroy(net_server* server);
net_socket* net_server_accept(net_server* server);  // Blocking accept

// Client functions
net_socket* net_client_connect(const char* host, uint16_t port);
void net_socket_close(net_socket* socket);

// Data transfer
int net_send(net_socket* socket, const uint8_t* data, size_t len);
int net_recv(net_socket* socket, uint8_t* buffer, size_t max_len);

// Non-blocking check if data available
bool net_has_data(net_socket* socket);

// Set socket options
void net_set_nonblocking(net_socket* socket, bool nonblocking);
void net_set_nodelay(net_socket* socket, bool nodelay);

#endif // NETWORK_H
