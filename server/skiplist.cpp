#include "skiplist.h"

namespace {

// Chance that a node gets one more level. With 1/4 (like Redis) a node has
// 1.33 levels on average, so the express lanes cost little memory.
const double LEVEL_UP_PROBABILITY = 0.25;

}  // namespace

SkipList::SkipList() {
    head_ = new Node{"", 0.0, std::vector<Level>(MAX_LEVEL)};
}

SkipList::~SkipList() {
    // Level 0 links every node, so walking it frees everything.
    Node* node = head_;
    while (node != nullptr) {
        Node* next = node->levels[0].next;
        delete node;
        node = next;
    }
}

int SkipList::random_level() {
    // One generator shared by every skip list. An std::mt19937 holds ~5 KB of
    // state; as a member it made SortedSet ~5 KB, and since a std::variant is
    // as big as its largest alternative, EVERY key (even a short string) cost
    // ~5 KB. Fixed seed: the same inserts always build the same shape.
    static std::mt19937 rng(12345);
    std::uniform_real_distribution<double> coin(0.0, 1.0);
    int level = 1;
    while (level < MAX_LEVEL && coin(rng) < LEVEL_UP_PROBABILITY) level++;
    return level;
}

bool SkipList::comes_before(const Node* node, double score, const std::string& member) {
    return node->score < score || (node->score == score && node->member < member);
}

void SkipList::insert(double score, const std::string& member) {
    Node* update[MAX_LEVEL];  // update[i] = last node on level i before the new node
    size_t rank[MAX_LEVEL];   // rank[i]   = position of update[i]

    // 1. Find where the new node goes, remembering the last node on each level.
    Node* x = head_;
    for (int i = level_ - 1; i >= 0; i--) {
        rank[i] = (i == level_ - 1) ? 0 : rank[i + 1];
        while (x->levels[i].next != nullptr && comes_before(x->levels[i].next, score, member)) {
            rank[i] += x->levels[i].span;
            x = x->levels[i].next;
        }
        update[i] = x;
    }

    // 2. Pick a random height. If it's taller than the list, the new top
    //    levels start at the head node.
    int new_level = random_level();
    if (new_level > level_) {
        for (int i = level_; i < new_level; i++) {
            rank[i] = 0;
            update[i] = head_;
            update[i]->levels[i].span = length_;  // head jumps over everything
        }
        level_ = new_level;
    }

    // 3. Link the node in on each of its levels and fix the spans.
    Node* node = new Node{member, score, std::vector<Level>(new_level)};
    for (int i = 0; i < new_level; i++) {
        node->levels[i].next = update[i]->levels[i].next;
        update[i]->levels[i].next = node;

        // rank[0] - rank[i] = nodes between update[i] and the new node.
        node->levels[i].span = update[i]->levels[i].span - (rank[0] - rank[i]);
        update[i]->levels[i].span = (rank[0] - rank[i]) + 1;
    }

    // 4. Higher links that pass over the new node now jump one extra node.
    for (int i = new_level; i < level_; i++) {
        update[i]->levels[i].span++;
    }
    length_++;
}

bool SkipList::remove(double score, const std::string& member) {
    Node* update[MAX_LEVEL];
    Node* x = head_;
    for (int i = level_ - 1; i >= 0; i--) {
        while (x->levels[i].next != nullptr && comes_before(x->levels[i].next, score, member)) {
            x = x->levels[i].next;
        }
        update[i] = x;
    }

    x = x->levels[0].next;  // the candidate node
    if (x == nullptr || x->score != score || x->member != member) return false;

    for (int i = 0; i < level_; i++) {
        if (update[i]->levels[i].next == x) {
            // Bypass x: the link now jumps x's span too (minus x itself).
            update[i]->levels[i].span += x->levels[i].span - 1;
            update[i]->levels[i].next = x->levels[i].next;
        } else {
            // This link jumped over x, so it is now one node shorter.
            update[i]->levels[i].span -= 1;
        }
    }
    delete x;

    // Drop empty top levels.
    while (level_ > 1 && head_->levels[level_ - 1].next == nullptr) level_--;
    length_--;
    return true;
}

long SkipList::rank(double score, const std::string& member) const {
    size_t traversed = 0;
    const Node* x = head_;
    for (int i = level_ - 1; i >= 0; i--) {
        // Move forward while the next node is <= (score, member).
        while (x->levels[i].next != nullptr &&
               (comes_before(x->levels[i].next, score, member) ||
                (x->levels[i].next->score == score && x->levels[i].next->member == member))) {
            traversed += x->levels[i].span;
            x = x->levels[i].next;
        }
        if (x != head_ && x->score == score && x->member == member) {
            return static_cast<long>(traversed) - 1;  // spans count from 1
        }
    }
    return -1;
}

const SkipList::Node* SkipList::node_at_rank(size_t rank) const {
    size_t traversed = 0;
    const Node* x = head_;
    for (int i = level_ - 1; i >= 0; i--) {
        // Jump forward as long as we don't overshoot the wanted rank.
        while (x->levels[i].next != nullptr && traversed + x->levels[i].span <= rank) {
            traversed += x->levels[i].span;
            x = x->levels[i].next;
        }
        if (traversed == rank) return x;
    }
    return nullptr;
}

std::vector<std::pair<std::string, double>> SkipList::range(size_t start, size_t stop) const {
    std::vector<std::pair<std::string, double>> result;
    if (start > stop || start >= length_) return result;

    // O(log n) jump to the first node, then walk level 0.
    const Node* x = node_at_rank(start + 1);
    for (size_t i = start; i <= stop && x != nullptr; i++) {
        result.emplace_back(x->member, x->score);
        x = x->levels[0].next;
    }
    return result;
}
