#include "glob.h"

bool glob_match(const std::string& pattern, const std::string& text) {
    const size_t NONE = std::string::npos;
    size_t p = 0;              // position in pattern
    size_t t = 0;              // position in text
    size_t star = NONE;        // position of the last '*' seen in pattern
    size_t star_text = 0;      // text position when that '*' was seen

    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            p++;  // characters match: advance both
            t++;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;       // remember the star; first try letting it match nothing
            star_text = t;
        } else if (star != NONE) {
            p = star + 1;     // mismatch: backtrack, let the star eat one more char
            t = ++star_text;
        } else {
            return false;     // mismatch and no star to fall back to
        }
    }
    while (p < pattern.size() && pattern[p] == '*') p++;  // trailing stars match empty
    return p == pattern.size();
}
