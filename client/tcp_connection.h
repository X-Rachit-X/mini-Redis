#pragma once

#include <string>

// A blocking TCP connection to the server.
// (The client handles one thing at a time, so blocking calls keep it simple.)
class TcpConnection {
public:
    TcpConnection() = default;
    ~TcpConnection();
    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

    // Resolves `host` (name or IP, IPv4 or IPv6) and connects.
    // Prints the reason and returns false on failure.
    bool connect_to(const std::string& host, int port);

    // Sends all bytes (send() may send only part of them per call).
    bool send_all(const std::string& data);

    void close();
    int fd() const { return fd_; }

private:
    int fd_ = -1;
};
