#pragma once

#include <string>
#include <vector>

// Functions that append RESP-encoded replies to an output buffer.
// Every reply type starts with one character that tells the client what follows.

void reply_simple(std::string& out, const std::string& text);    // +OK\r\n
void reply_error(std::string& out, const std::string& message);  // -ERR message\r\n
void reply_integer(std::string& out, long long value);           // :42\r\n
void reply_bulk(std::string& out, const std::string& data);      // $5\r\nhello\r\n
void reply_null(std::string& out);                               // $-1\r\n  (nil)
void reply_array_header(std::string& out, size_t count);         // *3\r\n  (then 3 replies)

// Encodes a command as a RESP array of bulk strings. Used for the AOF file.
std::string encode_command(const std::vector<std::string>& args);

// Shortest text form of a double that reads back as exactly the same value.
std::string format_double(double value);
