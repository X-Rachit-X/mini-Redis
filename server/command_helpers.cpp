#include "command_helpers.h"

#include <cctype>
#include <charconv>
#include <cmath>
#include <limits>

std::string to_upper(std::string text) {
    for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

std::string to_lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool parse_integer(const std::string& text, long long& out) {
    const char* begin = text.data();
    const char* end = begin + text.size();
    std::from_chars_result result = std::from_chars(begin, end, out);
    // ec == errc() means success; ptr == end means every character was used.
    return result.ec == std::errc() && result.ptr == end;
}

bool parse_score(const std::string& text, double& out) {
    // from_chars accepts "inf" and "-inf" but not "+inf", which Redis allows.
    if (text == "+inf") {
        out = std::numeric_limits<double>::infinity();
        return true;
    }
    const char* begin = text.data();
    const char* end = begin + text.size();
    std::from_chars_result result = std::from_chars(begin, end, out);
    return result.ec == std::errc() && result.ptr == end && !std::isnan(out);
}

bool normalize_range(long long& start, long long& stop, long long size) {
    if (start < 0) start += size;
    if (stop < 0) stop += size;
    if (start < 0) start = 0;
    if (stop >= size) stop = size - 1;
    return start <= stop && start < size;
}
