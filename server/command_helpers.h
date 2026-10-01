#pragma once

// Small helpers shared by all the cmd_*.cpp files.

#include <string>
#include <variant>

#include "commands.h"
#include "database.h"
#include "resp_writer.h"

// Error messages use the same text as real Redis, so clients behave the same.
inline const char* const ERR_WRONGTYPE =
    "WRONGTYPE Operation against a key holding the wrong kind of value";
inline const char* const ERR_NOT_INTEGER = "ERR value is not an integer or out of range";
inline const char* const ERR_NOT_FLOAT = "ERR value is not a valid float";
inline const char* const ERR_SYNTAX = "ERR syntax error";

// Largest number of seconds/ms we accept for EXPIRE / SET EX.
// Keeps "now + amount * 1000" far away from integer overflow.
const long long MAX_EXPIRE_AMOUNT = 1000000000000LL;

std::string to_upper(std::string text);
std::string to_lower(std::string text);

// Strict number parsing: the WHOLE text must be a number ("12abc" fails).
bool parse_integer(const std::string& text, long long& out);
bool parse_score(const std::string& text, double& out);

// Turns Redis-style [start, stop] indexes (negative = counted from the end,
// -1 is the last element) into valid 0-based indexes for a container of
// `size` elements. Returns false if the range is empty.
bool normalize_range(long long& start, long long& stop, long long size);

// Looks up `key` expecting a value of type T (std::string, List, Hash, SortedSet).
//   - key exists with type T      -> pointer to the value
//   - key does not exist          -> nullptr
//   - key exists with other type  -> nullptr and wrong_type = true
template <typename T>
T* find_typed(Database& db, const std::string& key, bool& wrong_type) {
    wrong_type = false;
    Entry* entry = db.find(key);
    if (entry == nullptr) return nullptr;
    T* value = std::get_if<T>(&entry->value);
    if (value == nullptr) wrong_type = true;
    return value;
}

// Same as find_typed, but creates an empty T if the key does not exist.
template <typename T>
T* find_or_create(Database& db, const std::string& key, bool& wrong_type) {
    T* value = find_typed<T>(db, key, wrong_type);
    if (value != nullptr || wrong_type) return value;
    Entry& entry = db.create(key);
    return &entry.value.emplace<T>();
}
