#pragma once

#include <cstdint>
#include <deque>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "sorted_set.h"

using List = std::deque<std::string>;  // deque: O(1) push/pop at BOTH ends
using Hash = std::unordered_map<std::string, std::string>;

// The value stored under a key is exactly ONE of these four types.
// std::variant remembers which one it holds (value.index() is 0..3).
using Value = std::variant<std::string, List, Hash, SortedSet>;

const int64_t NO_EXPIRY = -1;

struct Entry {
    Value value;
    int64_t expire_at = NO_EXPIRY;  // absolute time in ms, or NO_EXPIRY
};

// "string", "list", "hash" or "zset" (what the TYPE command returns).
const char* type_name(const Value& value);

// The keyspace: every key and its value, plus expiry bookkeeping.
//
// Expiry works two ways, like Redis:
//   - lazily: find() deletes a key if it has expired when someone touches it
//   - actively: remove_expired() is called ~10 times/second to clean up keys
//     nobody touches. `expiry_index_` keeps keys sorted by expiry time,
//     so the soonest-expiring key is always at the front.
class Database {
public:
    Database();

    // Returns the entry for `key`, or nullptr if it doesn't exist or has expired.
    // The pointer stays valid until this key is removed.
    Entry* find(const std::string& key);

    // Creates a fresh entry for `key` (holding an empty string), replacing
    // any existing value and TTL.
    Entry& create(const std::string& key);

    // Deletes the key. Returns true if something was deleted.
    bool remove(const std::string& key);

    // Moves the value (and TTL) of `from` to `to`, overwriting `to`.
    // Returns false if `from` does not exist.
    bool rename(const std::string& from, const std::string& to);

    // Sets an absolute expiry time (ms). If the time is already in the past,
    // the key is deleted right away. Returns false if the key doesn't exist.
    bool set_expire(const std::string& key, int64_t at_ms);

    // Removes the TTL. Returns true if the key had one.
    bool persist(const std::string& key);

    // Deletes up to `max_keys` expired keys. Returns how many were deleted.
    int remove_expired(int max_keys);

    void clear();
    size_t size() const { return data_.size(); }
    std::vector<std::string> keys() const;  // all keys that have not expired

    bool is_expired(const Entry& entry) const;
    int64_t now() const { return clock_(); }

    // Read-only access to every entry (used to rewrite the AOF).
    const std::unordered_map<std::string, Entry>& entries() const { return data_; }

    // Lets tests replace the clock so they can "travel in time".
    void set_clock(int64_t (*clock)()) { clock_ = clock; }

private:
    std::unordered_map<std::string, Entry> data_;
    std::set<std::pair<int64_t, std::string>> expiry_index_;  // (expire_at, key)
    int64_t (*clock_)();
};
