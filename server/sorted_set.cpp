#include "sorted_set.h"

bool SortedSet::add(const std::string& member, double score) {
    auto it = scores_.find(member);
    if (it == scores_.end()) {
        scores_.emplace(member, score);
        list_.insert(score, member);
        return true;
    }
    if (it->second != score) {
        // Existing member with a new score: move it to its new position.
        list_.remove(it->second, member);
        list_.insert(score, member);
        it->second = score;
    }
    return false;
}

bool SortedSet::remove(const std::string& member) {
    auto it = scores_.find(member);
    if (it == scores_.end()) return false;
    list_.remove(it->second, member);
    scores_.erase(it);
    return true;
}

bool SortedSet::score(const std::string& member, double& out) const {
    auto it = scores_.find(member);
    if (it == scores_.end()) return false;
    out = it->second;
    return true;
}

long SortedSet::rank(const std::string& member) const {
    auto it = scores_.find(member);
    if (it == scores_.end()) return -1;
    return list_.rank(it->second, member);
}

std::vector<std::pair<std::string, double>> SortedSet::range(size_t start, size_t stop) const {
    return list_.range(start, stop);
}
