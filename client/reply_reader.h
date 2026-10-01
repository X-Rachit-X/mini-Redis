#pragma once

#include <cstddef>
#include <string>

#include "reply.h"

// Reads RESP replies from a socket.
//
// It keeps its own buffer and reads from the socket in big chunks (one
// recv() per 16 KB instead of one per byte), then parses from the buffer.
class ReplyReader {
public:
    explicit ReplyReader(int fd) : fd_(fd) {}

    // Reads one complete reply (waits for it if needed).
    // Returns false if the connection closed or the server sent invalid data.
    bool read_reply(Reply& reply);

private:
    bool fill_buffer();                         // recv() more bytes into buffer_
    bool read_line(std::string& line);          // up to "\r\n" (not included)
    bool read_exact(size_t count, std::string& data);

    int fd_;
    std::string buffer_;  // bytes received from the socket
    size_t pos_ = 0;      // how much of buffer_ has been parsed already
};
