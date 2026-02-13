#include "network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>

net_server* net_server_create(uint16_t port) {
    net_server* server = (net_server*)malloc(sizeof(net_server));
    if (!server) return NULL;
    
    server->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->fd < 0) {
        free(server);
        return NULL;
    }
    
    // Allow address reuse
    int opt = 1;
    setsockopt(server->fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(server->fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(server->fd);
        free(server);
        return NULL;
    }
    
    if (listen(server->fd, 5) < 0) {
        close(server->fd);
        free(server);
        return NULL;
    }
    
    // Get actual port if 0 was passed
    if (port == 0) {
        socklen_t len = sizeof(addr);
        if (getsockname(server->fd, (struct sockaddr*)&addr, &len) == 0) {
            port = ntohs(addr.sin_port);
        }
    }
    
    server->port = port;
    server->listening = true;
    
    return server;
}

void net_server_destroy(net_server* server) {
    if (!server) return;
    
    if (server->fd >= 0) {
        close(server->fd);
    }
    server->listening = false;
    free(server);
}

net_socket* net_server_accept(net_server* server) {
    if (!server || !server->listening) return NULL;
    
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    int client_fd = accept(server->fd, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd < 0) {
        return NULL;
    }
    
    net_socket* socket = (net_socket*)malloc(sizeof(net_socket));
    if (!socket) {
        close(client_fd);
        return NULL;
    }
    
    socket->fd = client_fd;
    socket->connected = true;
    socket->port = ntohs(client_addr.sin_port);
    
    inet_ntop(AF_INET, &client_addr.sin_addr, socket->address, sizeof(socket->address));
    
    return socket;
}

net_socket* net_client_connect(const char* host, uint16_t port) {
    if (!host) return NULL;
    
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", port);
    
    if (getaddrinfo(host, port_str, &hints, &result) != 0) {
        return NULL;
    }
    
    int fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(result);
        return NULL;
    }
    
    if (connect(fd, result->ai_addr, result->ai_addrlen) < 0) {
        close(fd);
        freeaddrinfo(result);
        return NULL;
    }
    
    net_socket* socket = (net_socket*)malloc(sizeof(net_socket));
    if (!socket) {
        close(fd);
        freeaddrinfo(result);
        return NULL;
    }
    
    socket->fd = fd;
    socket->connected = true;
    socket->port = port;
    strncpy(socket->address, host, sizeof(socket->address) - 1);
    socket->address[sizeof(socket->address) - 1] = '\0';
    
    freeaddrinfo(result);
    return socket;
}

void net_socket_close(net_socket* socket) {
    if (!socket) return;
    
    if (socket->fd >= 0) {
        close(socket->fd);
    }
    socket->connected = false;
    free(socket);
}

int net_send(net_socket* socket, const uint8_t* data, size_t len) {
    if (!socket || !socket->connected || socket->fd < 0) return -1;
    if (!data || len == 0) return 0;
    
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(socket->fd, data + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Would block - return what we sent so far
                return (int)sent;
            }
            socket->connected = false;
            return -1;
        }
        if (n == 0) {
            socket->connected = false;
            return -1;
        }
        sent += n;
    }
    
    return (int)sent;
}

int net_recv(net_socket* socket, uint8_t* buffer, size_t max_len) {
    if (!socket || !socket->connected || socket->fd < 0) return -1;
    if (!buffer || max_len == 0) return 0;
    
    ssize_t n = recv(socket->fd, buffer, max_len, 0);
    if (n < 0) {
        if (errno == EINTR) return 0;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        socket->connected = false;
        return -1;
    }
    if (n == 0) {
        socket->connected = false;
        return -1;
    }
    
    return (int)n;
}

bool net_has_data(net_socket* socket) {
    if (!socket || !socket->connected || socket->fd < 0) return false;
    
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(socket->fd, &readfds);
    
    struct timeval tv = {0, 0};  // Immediate return
    
    int result = select(socket->fd + 1, &readfds, NULL, NULL, &tv);
    return result > 0 && FD_ISSET(socket->fd, &readfds);
}

void net_set_nonblocking(net_socket* socket, bool nonblocking) {
    if (!socket || socket->fd < 0) return;
    
    int flags = fcntl(socket->fd, F_GETFL, 0);
    if (flags < 0) return;
    
    if (nonblocking) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    
    fcntl(socket->fd, F_SETFL, flags);
}

void net_set_nodelay(net_socket* socket, bool nodelay) {
    if (!socket || socket->fd < 0) return;
    
    int flag = nodelay ? 1 : 0;
    setsockopt(socket->fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
}
