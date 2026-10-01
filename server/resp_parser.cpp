#include "resp_parser.h"

namespace {

// Safety limits, so a broken or malicious client cannot make us
// allocate unlimited memory.
const long long MAX_ARGS = 1024 * 1024;               // items in one command
const long long MAX_BULK_LENGTH = 512LL * 1024 * 1024;  // size of one argument (same as Redis)
const size_t MAX_LINE_LENGTH = 64 * 1024;             // a header line or an inline command

const size_t NOT_FOUND = std::string::npos;

// Returns the index of the first "\r\n" at or after `from`, or NOT_FOUND.
size_t find_crlf(const char* data, size_t len, size_t from) {
    for (size_t i = from; i + 1 < len; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n') return i;
    }
    return NOT_FOUND;
}

// Parses the decimal number written in data[start .. end).
// Returns false if it is not a number or is unreasonably large.
bool parse_number(const char* data, size_t start, size_t end, long long& out) {
    if (start >= end) return false;
    bool negative = false;
    if (data[start] == '-') {
        negative = true;
        start++;
        if (start == end) return false;
    }
    long long value = 0;
    for (size_t i = start; i < end; i++) {
        if (data[i] < '0' || data[i] > '9') return false;
        value = value * 10 + (data[i] - '0');
        if (value > MAX_BULK_LENGTH) return false;  // also prevents overflow
    }
    out = negative ? -value : value;
    return true;
}

bool is_space(char c) { return c == ' ' || c == '\t'; }

// Inline command: words separated by spaces, ending with "\n" or "\r\n".
ParseStatus parse_inline(const char* data, size_t len, std::vector<std::string>& args,
                         size_t& consumed, std::string& error) {
    size_t newline = NOT_FOUND;
    for (size_t i = 0; i < len; i++) {
        if (data[i] == '\n') {
            newline = i;
            break;
        }
    }
    if (newline == NOT_FOUND) {
        if (len > MAX_LINE_LENGTH) {
            error = "too big inline request";
            return ParseStatus::Error;
        }
        return ParseStatus::Incomplete;
    }

    size_t end = newline;
    if (end > 0 && data[end - 1] == '\r') end--;  // ignore the optional '\r'

    size_t i = 0;
    while (i < end) {
        while (i < end && is_space(data[i])) i++;      // skip spaces
        size_t word_start = i;
        while (i < end && !is_space(data[i])) i++;     // read one word
        if (i > word_start) args.emplace_back(data + word_start, i - word_start);
    }
    consumed = newline + 1;
    return ParseStatus::Ok;
}

// RESP array:  *<count>\r\n  followed by <count> bulk strings  $<len>\r\n<bytes>\r\n
ParseStatus parse_array(const char* data, size_t len, std::vector<std::string>& args,
                        size_t& consumed, std::string& error) {
    // 1. Header line "*<count>\r\n"
    size_t crlf = find_crlf(data, len, 1);
    if (crlf == NOT_FOUND) {
        if (len > MAX_LINE_LENGTH) {
            error = "too big multibulk header";
            return ParseStatus::Error;
        }
        return ParseStatus::Incomplete;
    }
    long long count;
    if (!parse_number(data, 1, crlf, count) || count > MAX_ARGS) {
        error = "invalid multibulk length";
        return ParseStatus::Error;
    }
    size_t pos = crlf + 2;
    if (count <= 0) {  // "*0\r\n" or "*-1\r\n": an empty command, just skip it
        consumed = pos;
        return ParseStatus::Ok;
    }

    // 2. Each argument "$<len>\r\n<bytes>\r\n"
    for (long long i = 0; i < count; i++) {
        if (pos >= len) return ParseStatus::Incomplete;
        if (data[pos] != '$') {
            error = "expected '$', got '" + std::string(1, data[pos]) + "'";
            return ParseStatus::Error;
        }
        crlf = find_crlf(data, len, pos + 1);
        if (crlf == NOT_FOUND) {
            if (len - pos > MAX_LINE_LENGTH) {
                error = "too big bulk header";
                return ParseStatus::Error;
            }
            return ParseStatus::Incomplete;
        }
        long long bulk_len;
        if (!parse_number(data, pos + 1, crlf, bulk_len) || bulk_len < 0) {
            error = "invalid bulk length";
            return ParseStatus::Error;
        }
        pos = crlf + 2;

        // The argument bytes plus the trailing "\r\n" must be fully in the buffer.
        // Note: we do NOT search for "\r\n" here, we trust the length. This is
        // what makes RESP "binary safe": the value itself may contain "\r\n".
        if (pos + bulk_len + 2 > len) return ParseStatus::Incomplete;
        if (data[pos + bulk_len] != '\r' || data[pos + bulk_len + 1] != '\n') {
            error = "bulk string not terminated by CRLF";
            return ParseStatus::Error;
        }
        args.emplace_back(data + pos, bulk_len);
        pos += bulk_len + 2;
    }
    consumed = pos;
    return ParseStatus::Ok;
}

}  // namespace

ParseStatus parse_command(const char* data, size_t len, std::vector<std::string>& args,
                          size_t& consumed, std::string& error) {
    args.clear();
    consumed = 0;
    if (len == 0) return ParseStatus::Incomplete;

    ParseStatus status = (data[0] == '*')
        ? parse_array(data, len, args, consumed, error)
        : parse_inline(data, len, args, consumed, error);

    if (status != ParseStatus::Ok) {
        args.clear();  // never hand out half a command
        consumed = 0;
    }
    return status;
}
