#pragma once

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "skiplist.h"

// A Redis sorted set (ZSET): unique members, each with a score,
// kept in order of score.
//
// It uses two structures that always hold the same members:
//   - a hash map   member -> score   for O(1) "what is the score of X?"
//   - a skip list  sorted by score   for O(log n) rank and range queries
class SortedSet {
public:
    // Adds the member or updates its score. Returns true if the member is new.
    bool add(const std::string& member, double score);

    // Returns false if the member was not in the set.
    bool remove(const std::string& member);

    // Looks up a member's score. Returns false if it is not in the set.
    bool score(const std::string& member, double& out) const;

    // 0-based position in sorted order, or -1 if not found.
    long rank(const std::string& member) const;

    // Members at positions start..stop (inclusive, already validated).
    std::vector<std::pair<std::string, double>> range(size_t start, size_t stop) const;

    size_t size() const { return scores_.size(); }

private:
    std::unordered_map<std::string, double> scores_;
    SkipList list_;
};
