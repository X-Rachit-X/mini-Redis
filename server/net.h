#pragma once

#include <string>

// Small wrappers around POSIX socket calls used by the server.

// Puts a file descriptor in non-blocking mode: read()/write()/accept()
// return immediately with EAGAIN instead of waiting.
bool set_nonblocking(int fd);

// Disables Nagle's algorithm so small replies are sent immediately
// instead of being delayed to be merged with later data.
void set_tcp_nodelay(int fd);

// Creates a non-blocking TCP socket listening on `bind_address` (an IPv4
// address such as "127.0.0.1", or "0.0.0.0" for every interface) at `port`.
// Returns the socket fd, or -1 on error (the reason is printed).
int create_listen_socket(const std::string& bind_address, int port);
