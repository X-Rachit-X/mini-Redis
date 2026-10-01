# 03: Skip list and sorted set

This is the most "algorithmic" part of the project and the best topic to show off in an interview.
Read section 8 of [the basics](../1-basics.md) first.

---

## A worked example of spans

Each link stores `span` = how many level-0 steps it jumps. The head counts as position 0:

```
positions:          0      1     2     3     4
level 1:  head ----(2)---> b ---(2)---------> d ---(0)--> nil
level 0:  head -(1)-> a -(1)-> b -(1)-> c -(1)-> d -(0)-> nil
```
(A link to `nil` jumps over no real nodes, so its span is 0.)

**Rank of `d`:** start at the head on level 1, add span 2 to reach `b`, add 2 to reach `d`. Total = 4, so `d` is the 4th element (1-based), rank 3 (0-based). Only 2 hops instead of 4.

That's the whole trick: **follow the express lanes and add up the spans.**

---

## `server/skiplist.h`

```cpp
class SkipList {
public:
    SkipList();
    ~SkipList();
    SkipList(const SkipList&) = delete;
    SkipList& operator=(const SkipList&) = delete;
```
The list owns nodes created with `new`. If C++ copied the object (its default copy copies the pointer), two lists would point to the same nodes and both destructors would `delete` them: a **double free**. `= delete` makes copying a compile error. This is the **rule of three**: if you write a destructor, decide what copying should do.

```cpp
    void insert(double score, const std::string& member);
    bool remove(double score, const std::string& member);
    long rank(double score, const std::string& member) const;
    std::vector<std::pair<std::string, double>> range(size_t start, size_t stop) const;
    size_t size() const { return length_; }
```
The list is ordered by `(score, member)`. `remove` and `rank` need the score too, because that's how we find the position. `SortedSet` looks the score up in its hash map first.

```cpp
private:
    struct Node;
    struct Level {
        Node* next = nullptr;
        size_t span = 0;
    };
    struct Node {
        std::string member;
        double score;
        std::vector<Level> levels;
    };
```
- `struct Node;` is a *forward declaration*, needed because `Level` mentions `Node*` before `Node` is fully defined.
- A node has a **variable number** of levels, so `levels` is a vector. `levels[0]` is the full linked list, `levels[1]` is the first express lane, and so on.

```cpp
    static const int MAX_LEVEL = 32;
```
With a 1/4 chance per level, reaching level 32 needs about 4^31 elements, which is unreachable. So 32 is effectively unlimited.

```cpp
    int random_level();
    static bool comes_before(const Node* node, double score, const std::string& member);
    const Node* node_at_rank(size_t rank) const;

    Node* head_;
    int level_ = 1;
    size_t length_ = 0;
};
```
- `head_` is a **sentinel** (dummy) node with all 32 levels. It's never a real element, but it means every real node always has a predecessor on every level, so no special case is needed for "insert at the front".
- `level_` is how many levels are actually in use (searches start there, not at 32).
- There is deliberately **no random generator member**. It lives inside `random_level()` instead (see below). An earlier version had `std::mt19937 rng_{12345};` here, which made every key in the database cost ~5 KB. The story is in [Layer 7, known issue #1](../7-defense-guide.md#known-issue-1-every-key-cost-about-5-kb-fixed).

---

## `server/skiplist.cpp`

```cpp
const double LEVEL_UP_PROBABILITY = 0.25;
```
Each node has level 1, and with probability 1/4 it also gets level 2, then 1/4 of those get level 3, and so on. The average is 1/(1−0.25) ≈ 1.33 levels per node, so it's memory-efficient. Redis uses the same value.

```cpp
SkipList::SkipList() {
    head_ = new Node{"", 0.0, std::vector<Level>(MAX_LEVEL)};
}
```
**Aggregate initialization** `Node{member, score, levels}`. The head gets all 32 levels, each `{nullptr, 0}`.

```cpp
SkipList::~SkipList() {
    Node* node = head_;
    while (node != nullptr) {
        Node* next = node->levels[0].next;
        delete node;
        node = next;
    }
}
```
Level 0 contains every node, so walk it and free each one. `next` is saved **before** `delete`, because reading `node->levels` after deleting it would be use-after-free.

```cpp
int SkipList::random_level() {
    static std::mt19937 rng(12345);
    std::uniform_real_distribution<double> coin(0.0, 1.0);
    int level = 1;
    while (level < MAX_LEVEL && coin(rng) < LEVEL_UP_PROBABILITY) level++;
    return level;
}
```
Flip a biased coin until it fails, counting heads. That's a geometric distribution.
- `std::mt19937` is a good pseudo-random generator. The **fixed seed** means the same sequence of inserts always builds the same shape, so bugs are reproducible.
- `static` inside the function means **one generator shared by every skip list**, created the first time any list needs a level. Why not a member? An `mt19937` carries about 5 KB of state. As a member, it made every `SortedSet` 5 KB. Because `Value` is a `std::variant` (always as big as its largest alternative), that made **every key** 5 KB, even a one-byte string. Shared, it costs 5 KB once for the whole process. Levels only need to be random, not independent per list, so sharing is safe. The server is single-threaded, so there's no data race on it either.

```cpp
bool SkipList::comes_before(const Node* node, double score, const std::string& member) {
    return node->score < score || (node->score == score && node->member < member);
}
```
The ordering rule: by score, and ties are broken alphabetically by member, exactly like Redis. Without a tie-breaker, members with equal scores would have no defined order.

### insert

```cpp
void SkipList::insert(double score, const std::string& member) {
    Node* update[MAX_LEVEL];
    size_t rank[MAX_LEVEL];
```
Two scratch arrays on the stack (cheap):
- `update[i]` is the **last node on level i that comes before** the new element. Its link on level i must be changed to point to the new node.
- `rank[i]` is the position of `update[i]`. We need it to compute the new spans.

```cpp
    Node* x = head_;
    for (int i = level_ - 1; i >= 0; i--) {
        rank[i] = (i == level_ - 1) ? 0 : rank[i + 1];
```
Search from the top lane down. On the top level we start at the head (position 0). On lower levels we continue from where the level above stopped, so we start with that rank.

```cpp
        while (x->levels[i].next != nullptr && comes_before(x->levels[i].next, score, member)) {
            rank[i] += x->levels[i].span;
            x = x->levels[i].next;
        }
        update[i] = x;
    }
```
Move right while the next node is still smaller, adding up spans to track our position. When the next node would be too big (or there is none), `x` is the predecessor on this level. Remember it, then drop down a level.

```cpp
    int new_level = random_level();
    if (new_level > level_) {
        for (int i = level_; i < new_level; i++) {
            rank[i] = 0;
            update[i] = head_;
            update[i]->levels[i].span = length_;
        }
        level_ = new_level;
    }
```
If the new node is taller than the list, the new top levels have no nodes yet. Their predecessor is the head (position 0). We set the head's span on those levels to `length_` (the link "jumps over everything to the end") so the span arithmetic below works the same for every level.

```cpp
    Node* node = new Node{member, score, std::vector<Level>(new_level)};
    for (int i = 0; i < new_level; i++) {
        node->levels[i].next = update[i]->levels[i].next;
        update[i]->levels[i].next = node;
```
Standard linked-list insert on each level: the new node points to what the predecessor pointed to, and the predecessor points to the new node.

```cpp
        node->levels[i].span = update[i]->levels[i].span - (rank[0] - rank[i]);
        update[i]->levels[i].span = (rank[0] - rank[i]) + 1;
    }
```
**The span arithmetic.** `rank[0]` is the position of the node just before the insertion point, so the new node will be at position `rank[0] + 1`. `rank[i]` is the position of the predecessor on level i.
- The predecessor's link now stops at the new node: distance = `(rank[0] + 1) - rank[i]` = `(rank[0] - rank[i]) + 1`.
- The node the predecessor used to point to was `old_span` steps away. After the insert it's one step further (`old_span + 1`), because the new node sits in between. The new node's link has to cover the distance from the new node to it: `(old_span + 1) - ((rank[0] - rank[i]) + 1)` = `old_span - (rank[0] - rank[i])`. That's the first line.

Example: insert `c2` between `c` and `d` above. On level 1, `update[1] = b` (rank 2), `rank[0] = 3` (node `c`). `b`'s new span = (3 − 2) + 1 = 2 (b → c → c2) only if c2 gets level 2. Otherwise the loop below just adds 1 to b's span (it now jumps c, c2, d = 3).

```cpp
    for (int i = new_level; i < level_; i++) {
        update[i]->levels[i].span++;
    }
    length_++;
}
```
Levels **above** the new node aren't relinked, but their links now jump over one more node, so the span grows by 1.

### remove

```cpp
bool SkipList::remove(double score, const std::string& member) {
    Node* update[MAX_LEVEL];
    Node* x = head_;
    for (int i = level_ - 1; i >= 0; i--) {
        while (x->levels[i].next != nullptr && comes_before(x->levels[i].next, score, member)) {
            x = x->levels[i].next;
        }
        update[i] = x;
    }
```
The same search, but we don't need ranks.

```cpp
    x = x->levels[0].next;
    if (x == nullptr || x->score != score || x->member != member) return false;
```
The node right after the level-0 predecessor is the only candidate. Check that it's really our element.

```cpp
    for (int i = 0; i < level_; i++) {
        if (update[i]->levels[i].next == x) {
            update[i]->levels[i].span += x->levels[i].span - 1;
            update[i]->levels[i].next = x->levels[i].next;
        } else {
            update[i]->levels[i].span -= 1;
        }
    }
    delete x;
```
On levels where `x` is linked, the predecessor skips past `x` and its span becomes "my span + x's span − 1 (x itself is gone)". On higher levels the link jumped *over* `x`, so it's now one shorter.

```cpp
    while (level_ > 1 && head_->levels[level_ - 1].next == nullptr) level_--;
    length_--;
    return true;
}
```
If we removed the only node on the top level(s), shrink `level_` so future searches don't start on empty lanes.

### rank

```cpp
long SkipList::rank(double score, const std::string& member) const {
    size_t traversed = 0;
    const Node* x = head_;
    for (int i = level_ - 1; i >= 0; i--) {
        while (x->levels[i].next != nullptr &&
               (comes_before(x->levels[i].next, score, member) ||
                (x->levels[i].next->score == score && x->levels[i].next->member == member))) {
            traversed += x->levels[i].span;
            x = x->levels[i].next;
        }
```
Same descent, but we move forward while the next node is **less than or equal to** the target, so we can land *on* it. We add spans as we go.

```cpp
        if (x != head_ && x->score == score && x->member == member) {
            return static_cast<long>(traversed) - 1;
        }
    }
    return -1;
}
```
As soon as we stand on the target (possibly already on a high level, which is even faster), the sum of spans is its 1-based position, so subtract 1 for Redis's 0-based rank.

### node_at_rank and range

```cpp
const SkipList::Node* SkipList::node_at_rank(size_t rank) const {
    size_t traversed = 0;
    const Node* x = head_;
    for (int i = level_ - 1; i >= 0; i--) {
        while (x->levels[i].next != nullptr && traversed + x->levels[i].span <= rank) {
            traversed += x->levels[i].span;
            x = x->levels[i].next;
        }
        if (traversed == rank) return x;
    }
    return nullptr;
}
```
The reverse question: "which node is at position N?" Take every jump that doesn't overshoot N. This is O(log n). The return type `const SkipList::Node*` needs the class prefix because the return type is written before the compiler knows we're inside `SkipList`.

```cpp
std::vector<std::pair<std::string, double>> SkipList::range(size_t start, size_t stop) const {
    std::vector<std::pair<std::string, double>> result;
    if (start > stop || start >= length_) return result;
    const Node* x = node_at_rank(start + 1);
    for (size_t i = start; i <= stop && x != nullptr; i++) {
        result.emplace_back(x->member, x->score);
        x = x->levels[0].next;
    }
    return result;
}
```
`ZRANGE` is an O(log n) jump to the first wanted element, then a walk along level 0 for the elements we return. That's O(log n + k), which is optimal.

---

## `server/sorted_set.h` / `.cpp`

```cpp
class SortedSet {
    ...
private:
    std::unordered_map<std::string, double> scores_;
    SkipList list_;
};
```
Two views of the same data:
- `scores_` answers "what's alice's score?" and "is bob in the set?" in O(1).
- `list_` answers everything about order in O(log n).

Because `SkipList` can't be copied, `SortedSet` can't either (the compiler deletes its copy constructor automatically). That's why the database builds entries in place (`try_emplace`, `emplace<T>()`).

```cpp
bool SortedSet::add(const std::string& member, double score) {
    auto it = scores_.find(member);
    if (it == scores_.end()) {
        scores_.emplace(member, score);
        list_.insert(score, member);
        return true;
    }
```
A new member goes into both structures. We return `true` because `ZADD` replies with the number of **new** members.

```cpp
    if (it->second != score) {
        list_.remove(it->second, member);
        list_.insert(score, member);
        it->second = score;
    }
    return false;
}
```
An existing member with a new score has to **move**: remove it at the old position (found via the old score), then insert it at the new one. Same score means nothing to do.

```cpp
bool SortedSet::remove(const std::string& member) {
    auto it = scores_.find(member);
    if (it == scores_.end()) return false;
    list_.remove(it->second, member);
    scores_.erase(it);
    return true;
}
```
Remove from both, always together. That invariant is what the randomized test verifies.

```cpp
bool SortedSet::score(const std::string& member, double& out) const { ... }
long SortedSet::rank(const std::string& member) const {
    auto it = scores_.find(member);
    if (it == scores_.end()) return -1;
    return list_.rank(it->second, member);
}
```
`rank` first gets the score from the hash map (O(1)), then asks the skip list (O(log n)).

---

## Complexity summary

| Operation | Command | Cost |
|---|---|---|
| add / update | ZADD | O(log n) |
| remove | ZREM | O(log n) |
| score | ZSCORE | O(1) |
| rank | ZRANK | O(log n) |
| range of k items | ZRANGE | O(log n + k) |
| count | ZCARD | O(1) |
