#pragma once

#include <string>

// Matches `text` against a glob pattern (used by the KEYS command):
//   *  matches any sequence of characters (also empty)
//   ?  matches exactly one character
// Example: glob_match("user:*", "user:42") == true
bool glob_match(const std::string& pattern, const std::string& text);
