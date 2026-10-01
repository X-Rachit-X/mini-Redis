#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Result of trying to parse one command out of a byte buffer.
enum class ParseStatus {
    Ok,          // one full command was parsed
    Incomplete,  // the buffer ends in the middle of a command: wait for more bytes
    Error        // the bytes are not valid RESP: the connection should be closed
};

// Tries to parse ONE client command from data[0 .. len).
//
// Clients normally send commands as a RESP array of bulk strings:
//     *2\r\n$3\r\nGET\r\n$3\r\nfoo\r\n      ->  {"GET", "foo"}
// We also accept "inline" commands (a plain text line), which is what you
// type when you connect with telnet or nc:
//     GET foo\r\n                           ->  {"GET", "foo"}
//
// On Ok:         `args` holds the command and `consumed` = number of bytes used.
// On Incomplete: nothing is consumed; call again when more data has arrived.
// On Error:      `error` describes the problem.
ParseStatus parse_command(const char* data, size_t len,
                          std::vector<std::string>& args,
                          size_t& consumed,
                          std::string& error);
