// Micro benchmarks that justify the main data-structure decisions.
// Run with: make microbench

#include <chrono>
#include <cstdio>
#include <deque>
#include <iterator>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "../server/resp_parser.h"
#include "../server/sorted_set.h"

namespace {

// Runs `work` once and returns how long it took in milliseconds.
template <typename Work>
double time_ms(Work work) {
    auto start = std::chrono::steady_clock::now();
    work();
    auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// 1. Why lists use std::deque: LPUSH on a vector must shift every element.
void bench_list_push_front() {
    const int N = 50000;
    double vector_ms = time_ms([&] {
        std::vector<std::string> v;
        for (int i = 0; i < N; i++) v.insert(v.begin(), "item");
    });
    double deque_ms = time_ms([&] {
        std::deque<std::string> d;
        for (int i = 0; i < N; i++) d.push_front("item");
    });
    printf("LPUSH x %d          vector: %9.2f ms   deque: %7.2f ms   (%.0fx faster)\n",
           N, vector_ms, deque_ms, vector_ms / deque_ms);
}

// 2. How fast the RESP parser chews through a pipelined buffer.
void bench_parser() {
    const int N = 1000000;
    std::string one = "*3\r\n$3\r\nSET\r\n$10\r\nkey:000001\r\n$10\r\nvalue:0001\r\n";
    std::string buffer;
    buffer.reserve(one.size() * N);
    for (int i = 0; i < N; i++) buffer += one;

    std::vector<std::string> args;
    size_t parsed = 0;
    double ms = time_ms([&] {
        size_t pos = 0, consumed = 0;
        std::string error;
        while (parse_command(buffer.data() + pos, buffer.size() - pos, args, consumed, error) ==
               ParseStatus::Ok) {
            pos += consumed;
            parsed++;
        }
    });
    double mb = buffer.size() / (1024.0 * 1024.0);
    printf("RESP parse %zu cmds  %.2f ms   -> %.1f M commands/s, %.0f MB/s\n",
           parsed, ms, parsed / ms / 1000.0, mb / (ms / 1000.0));
}

// 3. Why the skip list stores spans: ZRANK in O(log n) instead of O(n).
void bench_rank() {
    const int N = 200000;
    const int QUERIES = 200;
    std::mt19937 rng(7);
    SortedSet zset;
    std::set<std::pair<double, std::string>> plain_set;
    std::vector<std::string> members;
    for (int i = 0; i < N; i++) {
        std::string member = "member:" + std::to_string(i);
        double score = static_cast<double>(rng() % 1000000);
        zset.add(member, score);
        plain_set.insert({score, member});
        members.push_back(member);
    }

    long sink = 0;  // use the results so the compiler can't skip the work
    double skiplist_ms = time_ms([&] {
        for (int q = 0; q < QUERIES; q++) sink += zset.rank(members[rng() % N]);
    });
    double set_ms = time_ms([&] {
        for (int q = 0; q < QUERIES; q++) {
            const std::string& member = members[rng() % N];
            double score;
            zset.score(member, score);
            // std::set has no rank operation: we must walk from the beginning.
            sink += std::distance(plain_set.begin(), plain_set.find({score, member}));
        }
    });
    printf("ZRANK x %d (n=%d) skiplist: %7.2f ms   std::set walk: %9.2f ms   (%.0fx faster)  [%ld]\n",
           QUERIES, N, skiplist_ms, set_ms, set_ms / skiplist_ms, sink % 10);
}

}  // namespace

int main() {
    printf("mini-redis micro benchmarks\n\n");
    bench_list_push_front();
    bench_parser();
    bench_rank();
    return 0;
}
