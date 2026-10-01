#include "tcp_connection.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

TcpConnection::~TcpConnection() {
    close();
}

bool TcpConnection::connect_to(const std::string& host, int port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;      // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;  // TCP

    addrinfo* results = nullptr;
    int err = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &results);
    if (err != 0) {
        std::cerr << "Could not resolve " << host << ": " << gai_strerror(err) << "\n";
        return false;
    }

    // A host name can resolve to several addresses: try each until one works.
    for (addrinfo* addr = results; addr != nullptr; addr = addr->ai_next) {
        fd_ = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (fd_ < 0) continue;
        if (connect(fd_, addr->ai_addr, addr->ai_addrlen) == 0) break;  // success
        ::close(fd_);
        fd_ = -1;
    }
    freeaddrinfo(results);

    if (fd_ < 0) {
        std::cerr << "Could not connect to " << host << ":" << port << "\n";
        return false;
    }
    return true;
}

bool TcpConnection::send_all(const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        // MSG_NOSIGNAL: a closed server gives an error instead of killing us with SIGPIPE.
        ssize_t n = send(fd_, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

void TcpConnection::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}
