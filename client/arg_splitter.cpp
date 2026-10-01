#include "arg_splitter.h"

#include <cctype>

namespace {

char unescape(char c) {
    switch (c) {
        case 'n': return '\n';
        case 'r': return '\r';
        case 't': return '\t';
        default:  return c;  // \" -> "   and   \\ -> backslash
    }
}

bool is_space(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

}  // namespace

bool split_args(const std::string& line, std::vector<std::string>& args) {
    args.clear();
    size_t i = 0;
    const size_t n = line.size();

    while (true) {
        while (i < n && is_space(line[i])) i++;  // skip spaces between arguments
        if (i >= n) return true;                 // end of line: done

        std::string current;
        bool in_double_quotes = false;
        bool in_single_quotes = false;

        // Read one argument. It ends at a space that is not inside quotes.
        while (i < n) {
            char c = line[i];
            if (in_double_quotes) {
                if (c == '\\' && i + 1 < n) {
                    current += unescape(line[i + 1]);
                    i += 2;
                    continue;
                }
                if (c == '"') in_double_quotes = false;
                else current += c;
            } else if (in_single_quotes) {
                if (c == '\'') in_single_quotes = false;
                else current += c;
            } else {
                if (is_space(c)) break;
                if (c == '"') in_double_quotes = true;
                else if (c == '\'') in_single_quotes = true;
                else current += c;
            }
            i++;
        }

        if (in_double_quotes || in_single_quotes) return false;  // unclosed quote
        args.push_back(current);
    }
}
