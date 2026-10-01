#include "reply_printer.h"

#include <cctype>
#include <cstdio>

namespace {

// Wraps a string in quotes and escapes special characters, so you can see
// exactly what is stored (e.g. a newline is shown as \n).
std::string quote(const std::string& text) {
    std::string out = "\"";
    for (unsigned char c : text) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (std::isprint(c)) {
                    out += static_cast<char>(c);
                } else {
                    char hex[8];
                    snprintf(hex, sizeof(hex), "\\x%02x", c);
                    out += hex;
                }
        }
    }
    return out + "\"";
}

}  // namespace

std::string format_reply(const Reply& reply, const std::string& indent) {
    switch (reply.type) {
        case Reply::Type::Status:  return reply.text;
        case Reply::Type::Error:   return "(error) " + reply.text;
        case Reply::Type::Integer: return "(integer) " + std::to_string(reply.integer);
        case Reply::Type::Bulk:    return quote(reply.text);
        case Reply::Type::Nil:     return "(nil)";
        case Reply::Type::Array:   break;
    }

    if (reply.elements.empty()) return "(empty array)";

    // Numbers are right-aligned: " 9) ..." and "10) ..." line up.
    size_t width = std::to_string(reply.elements.size()).size();
    std::string out;
    for (size_t i = 0; i < reply.elements.size(); i++) {
        std::string number = std::to_string(i + 1);
        std::string prefix = std::string(width - number.size(), ' ') + number + ") ";
        if (i > 0) out += "\n" + indent;
        // A nested array's lines are indented to start under its first item.
        out += prefix + format_reply(reply.elements[i], indent + std::string(prefix.size(), ' '));
    }
    return out;
}
