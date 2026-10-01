#include "net.h"

#include <arpa/inet.h>
#include <cstdio>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

void set_tcp_nodelay(int fd) {
    int yes = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
}

int create_listen_socket(const std::string& bind_address, int port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    // inet_pton turns text like "127.0.0.1" into the 4-byte binary address.
    if (inet_pton(AF_INET, bind_address.c_str(), &addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid bind address: %s\n", bind_address.c_str());
        return -1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    // Allow restarting the server right away on the same port
    // (otherwise bind() fails for ~60s while old connections are in TIME_WAIT).
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }
    if (listen(fd, SOMAXCONN) < 0) {
        perror("listen");
        close(fd);
        return -1;
    }
    if (!set_nonblocking(fd)) {
        perror("fcntl");
        close(fd);
        return -1;
    }
    return fd;
}
