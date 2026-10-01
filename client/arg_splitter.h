#pragma once

#include <string>
#include <vector>

// Splits a typed line into arguments, like a shell does:
//   SET name "Alice Smith"     ->  {"SET", "name", "Alice Smith"}
//   SET empty ""               ->  {"SET", "empty", ""}
//   SET msg "line1\nline2"     ->  escapes \n \r \t \" \\ work inside "..."
//   SET raw 'no \n escapes'    ->  single quotes keep text as is
// Returns false if a quote is not closed.
bool split_args(const std::string& line, std::vector<std::string>& args);
