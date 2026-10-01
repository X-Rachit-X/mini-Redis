#pragma once

#include <cstddef>
#include <string>

// State the server keeps for each connected client.
//
// With non-blocking sockets, data arrives in random-sized pieces and the
// socket may not accept all our output at once, so both directions need
// a buffer.
struct Connection {
    int fd = -1;

    std::string input;      // bytes received but not yet parsed into commands
    std::string output;     // reply bytes waiting to be sent
    size_t output_sent = 0; // how many bytes of `output` were already sent

    bool watching_writes = false;  // are we asking epoll for EPOLLOUT?
    bool close_after_reply = false; // protocol error: send the error, then close
};
