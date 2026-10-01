// Tests for the skip list / sorted set. A random test compares it with a
// simple, obviously-correct model (std::set) after thousands of operations.

#include <map>
#include <random>
#include <set>

#include "../server/sorted_set.h"
#include "test_framework.h"

TEST(sorted_set_basic_operations) {
    SortedSet z;
    CHECK(z.add("bob", 20));
    CHECK(z.add("alice", 10));
    CHECK(z.add("carol", 30));
    CHECK(!z.add("bob", 5));  // existing member: score updated, not "added"
    CHECK_EQ(z.size(), size_t(3));

    auto all = z.range(0, 2);
    CHECK_EQ(all[0].first, std::string("bob"));  // 5 is now the lowest
    CHECK_EQ(all[1].first, std::string("alice"));
    CHECK_EQ(all[2].first, std::string("carol"));
    CHECK_EQ(z.rank("carol"), 2L);
    CHECK_EQ(z.rank("nobody"), -1L);

    double score = 0;
    CHECK(z.score("alice", score));
    CHECK_EQ(score, 10.0);
    CHECK(z.remove("alice"));
    CHECK(!z.remove("alice"));
    CHECK_EQ(z.rank("carol"), 1L);
}

TEST(equal_scores_are_ordered_by_member) {
    SortedSet z;
    z.add("b", 1);
    z.add("c", 1);
    z.add("a", 1);
    auto all = z.range(0, 2);
    CHECK_EQ(all[0].first, std::string("a"));
    CHECK_EQ(all[2].first, std::string("c"));
}

TEST(sorted_set_matches_reference_model) {
    SortedSet z;
    std::map<std::string, double> scores;              // model: member -> score
    std::set<std::pair<double, std::string>> ordered;  // model: sorted order
    std::mt19937 rng(42);

    for (int op = 0; op < 20000; op++) {
        std::string member = "m" + std::to_string(rng() % 300);
        double score = static_cast<double>(rng() % 50);  // small range -> many ties
        if (rng() % 3 == 0) {
            bool removed = z.remove(member);
            CHECK_EQ(removed, scores.count(member) == 1);
            if (scores.count(member)) {
                ordered.erase({scores[member], member});
                scores.erase(member);
            }
        } else {
            bool added = z.add(member, score);
            CHECK_EQ(added, scores.count(member) == 0);
            if (scores.count(member)) ordered.erase({scores[member], member});
            scores[member] = score;
            ordered.insert({score, member});
        }

        if (op % 500 == 0 && !ordered.empty()) {
            // Whole range must match the model exactly.
            auto items = z.range(0, z.size() - 1);
            CHECK_EQ(items.size(), ordered.size());
            size_t index = 0;
            for (const auto& [model_score, model_member] : ordered) {
                CHECK_EQ(items[index].first, model_member);
                CHECK_EQ(items[index].second, model_score);
                CHECK_EQ(z.rank(model_member), static_cast<long>(index));
                index++;
            }
            // A sub-range in the middle.
            size_t start = ordered.size() / 3, stop = ordered.size() / 2;
            auto middle = z.range(start, stop);
            auto it = std::next(ordered.begin(), static_cast<long>(start));
            for (const auto& item : middle) {
                CHECK_EQ(item.first, it->second);
                ++it;
            }
        }
    }
}
