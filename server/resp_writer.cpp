#include "resp_writer.h"

#include <charconv>
#include <cmath>

namespace {

// Simple strings and errors are one line, so they must not contain CR or LF.
void append_single_line(std::string& out, const std::string& text) {
    for (char c : text) out += (c == '\r' || c == '\n') ? ' ' : c;
    out += "\r\n";
}

}  // namespace

void reply_simple(std::string& out, const std::string& text) {
    out += '+';
    append_single_line(out, text);
}

void reply_error(std::string& out, const std::string& message) {
    out += '-';
    append_single_line(out, message);
}

void reply_integer(std::string& out, long long value) {
    out += ':';
    out += std::to_string(value);
    out += "\r\n";
}

void reply_bulk(std::string& out, const std::string& data) {
    out += '$';
    out += std::to_string(data.size());
    out += "\r\n";
    out += data;
    out += "\r\n";
}

void reply_null(std::string& out) {
    out += "$-1\r\n";
}

void reply_array_header(std::string& out, size_t count) {
    out += '*';
    out += std::to_string(count);
    out += "\r\n";
}

std::string encode_command(const std::vector<std::string>& args) {
    std::string out;
    reply_array_header(out, args.size());
    for (const std::string& arg : args) reply_bulk(out, arg);
    return out;
}

std::string format_double(double value) {
    if (std::isinf(value)) return value > 0 ? "inf" : "-inf";
    char buffer[64];
    // std::to_chars picks the shortest text that round-trips: 1.1 -> "1.1"
    // (printf("%.17g") would give "1.1000000000000001").
    std::to_chars_result result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return std::string(buffer, result.ptr);
}
