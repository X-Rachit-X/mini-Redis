#pragma once

#include <cstddef>
#include <random>
#include <string>
#include <utility>
#include <vector>

// A skip list keeps (score, member) pairs sorted by score, then by member.
//
// Think of a sorted linked list with "express lanes" stacked on top:
//
//   level 2:  head ----------------------> 30 ----------------> nil
//   level 1:  head --------> 10 ---------> 30 ------> 50 -----> nil
//   level 0:  head --> 5 --> 10 --> 20 --> 30 --> 40 -> 50 ---> nil
//
// Every node is randomly given a height. Searching starts on the top lane
// and drops down a level whenever the next jump would go too far, so
// search / insert / remove take O(log n) on average.
//
// Each link also stores its "span" = how many level-0 nodes it jumps over.
// Adding up spans while searching gives a node's position (rank) in
// O(log n), which makes ZRANK and ZRANGE fast. This is the same design
// that real Redis uses for sorted sets.
class SkipList {
public:
    SkipList();
    ~SkipList();
    // The list owns raw pointers, so copying it would cause double frees.
    SkipList(const SkipList&) = delete;
    SkipList& operator=(const SkipList&) = delete;

    // Adds a pair. The caller guarantees `member` is not already in the list.
    void insert(double score, const std::string& member);

    // Removes the pair. Returns false if it was not found.
    bool remove(double score, const std::string& member);

    // 0-based position of the pair in sorted order, or -1 if not found.
    long rank(double score, const std::string& member) const;

    // Pairs at positions start..stop (inclusive, 0-based, already validated).
    std::vector<std::pair<std::string, double>> range(size_t start, size_t stop) const;

    size_t size() const { return length_; }

private:
    struct Node;
    struct Level {
        Node* next = nullptr;  // next node on this level
        size_t span = 0;       // how many level-0 steps this link jumps
    };
    struct Node {
        std::string member;
        double score;
        std::vector<Level> levels;  // levels[0] is the bottom (complete) list
    };

    static const int MAX_LEVEL = 32;

    int random_level();
    static bool comes_before(const Node* node, double score, const std::string& member);
    const Node* node_at_rank(size_t rank) const;  // rank is 1-based here

    Node* head_;        // dummy node before the first real node, has MAX_LEVEL levels
    int level_ = 1;     // number of levels currently in use
    size_t length_ = 0;
};
