# 02: Storage: `database` and `clock`

---

## `server/clock.h` / `clock.cpp`

```cpp
int64_t now_ms();
```
One function: the current time in milliseconds since 1 Jan 1970 (Unix epoch). `int64_t` is a 64-bit integer, big enough for millions of years of milliseconds.

```cpp
int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}
```
- `system_clock::now()`: the wall-clock time ("what time is it").
- `.time_since_epoch()`: the duration since 1970.
- `duration_cast<milliseconds>`: convert it to milliseconds.
- `.count()`: get the plain number.

**Why `system_clock` and not `steady_clock`?** Your old code used `steady_clock`, which measures time since an arbitrary point (often the boot). That's perfect for measuring intervals, but its values mean nothing after a restart. Expiry times are saved in the AOF, so they must be absolute.

---

## `server/database.h`

```cpp
#include "sorted_set.h"
```
`Value` contains a `SortedSet`, so the full type must be known here.

```cpp
using List = std::deque<std::string>;
using Hash = std::unordered_map<std::string, std::string>;
```
`using` creates short names (type aliases). Code then says `List` and `Hash`, which reads like Redis.

```cpp
using Value = std::variant<std::string, List, Hash, SortedSet>;
```
**The core design decision.** A `std::variant` holds exactly **one** of the listed types at a time and remembers which one (`value.index()` returns 0, 1, 2 or 3). Your old design had three separate maps, so the same key could exist as a string *and* a list. That's impossible here.

```cpp
const int64_t NO_EXPIRY = -1;

struct Entry {
    Value value;
    int64_t expire_at = NO_EXPIRY;
};
```
What's stored under each key: the value, plus its expiry time in absolute ms (or -1 for "never"). Keeping the TTL **inside** the entry means deleting the entry deletes the TTL too. Your old separate `expiry_map` could get out of sync (e.g. `FLUSHALL` forgot it).

```cpp
const char* type_name(const Value& value);
```
For the `TYPE` command.

```cpp
class Database {
public:
    Database();
    Entry* find(const std::string& key);
```
Returns a **pointer** so we can return `nullptr` for "not found", and so the caller can modify the entry in place (e.g. push to a list) without copying. Elements of `std::unordered_map` never move in memory when other keys are added (only iterators are invalidated on rehash, not pointers). So the pointer stays valid until *this* key is removed.

```cpp
    Entry& create(const std::string& key);
    bool remove(const std::string& key);
    bool rename(const std::string& from, const std::string& to);
    bool set_expire(const std::string& key, int64_t at_ms);
    bool persist(const std::string& key);
    int remove_expired(int max_keys);
    void clear();
    size_t size() const { return data_.size(); }
    std::vector<std::string> keys() const;
    bool is_expired(const Entry& entry) const;
    int64_t now() const { return clock_(); }
```
The whole storage API. The `const` methods promise not to modify the database. Commands call `db.now()` instead of `now_ms()` directly, which is what lets tests fake time.

```cpp
    const std::unordered_map<std::string, Entry>& entries() const { return data_; }
```
Read-only access for the AOF rewrite. A `const&` gives no copy and no modification.

```cpp
    void set_clock(int64_t (*clock)()) { clock_ = clock; }
```
`int64_t (*clock)()` is a **function pointer**: a variable holding "a function that takes nothing and returns int64_t". By default it's `now_ms`. Tests swap in a fake clock and move time forward instantly instead of sleeping. This is a simple form of **dependency injection**.

```cpp
private:
    std::unordered_map<std::string, Entry> data_;
    std::set<std::pair<int64_t, std::string>> expiry_index_;
    int64_t (*clock_)();
};
```
- `data_`: the keyspace. A hash table gives O(1) average lookup.
- `expiry_index_`: every key that has a TTL, as `(expire_at, key)` pairs. `std::set` keeps them **sorted**, and pairs compare by the first element (time), then the second. So `begin()` is always the key that expires soonest. Including the key in the pair makes entries unique even when two keys expire at the same millisecond.
- The trailing underscore is a common naming convention for member variables.

---

## `server/database.cpp`

```cpp
const char* type_name(const Value& value) {
    switch (value.index()) {
        case 0: return "string";
        case 1: return "list";
        case 2: return "hash";
        case 3: return "zset";
    }
    return "none";
}
```
`index()` follows the order of the types in the variant declaration. We return `const char*` (string literals live forever), so nothing is allocated.

```cpp
Database::Database() : clock_(now_ms) {}
```
The constructor's **member initializer list** sets the clock to the real one.

```cpp
bool Database::is_expired(const Entry& entry) const {
    return entry.expire_at != NO_EXPIRY && entry.expire_at <= clock_();
}
```
Expired means it has a TTL and that moment has arrived. `<=` means a key with TTL 500 ms is gone *at* 500 ms.

```cpp
Entry* Database::find(const std::string& key) {
    auto it = data_.find(key);
    if (it == data_.end()) return nullptr;
    if (is_expired(it->second)) {
        remove(key);
        return nullptr;
    }
    return &it->second;
}
```
**Lazy expiry** lives here. Every command reads keys through `find()`, so no command can ever see an expired key. `it->second` is the `Entry` (`first` is the key).

```cpp
Entry& Database::create(const std::string& key) {
    remove(key);
    return data_.try_emplace(key).first->second;
}
```
- `remove(key)` first, so the old value **and its TTL index entry** are gone. That's why `SET` clears an old TTL, fixing the bug in your old server.
- `try_emplace(key)` constructs a new `Entry` **directly inside** the map (default value: an empty string, the first variant type). It returns `pair<iterator, bool>`, and `.first->second` is a reference to the new entry.
- Why not `data_[key] = Entry{}`? That would build a temporary `Entry` and move it in. `SortedSet` (it owns raw pointers) can't be copied or moved, so in-place construction is required.

```cpp
bool Database::remove(const std::string& key) {
    auto it = data_.find(key);
    if (it == data_.end()) return false;
    if (it->second.expire_at != NO_EXPIRY) {
        expiry_index_.erase({it->second.expire_at, key});
    }
    data_.erase(it);
    return true;
}
```
The **single place** where keys die, so the index is always kept in sync. `erase({time, key})` builds the pair and removes it from the set in O(log n).

```cpp
bool Database::rename(const std::string& from, const std::string& to) {
    Entry* source = find(from);
    if (source == nullptr) return false;
    if (from == to) return true;
```
`find` (not `data_.find`), so an expired source counts as missing. Renaming a key to itself is a no-op. Without this check, the next steps would delete it.

```cpp
    int64_t expire_at = source->expire_at;
    if (expire_at != NO_EXPIRY) expiry_index_.erase({expire_at, from});
    remove(to);
```
Take the source out of the expiry index (it will come back under its new name), then delete whatever was at `to`, including *its* TTL.

```cpp
    auto node = data_.extract(from);
    node.key() = to;
    data_.insert(std::move(node));
```
**C++17 node handles.** `extract` unlinks the map node (key + value) without destroying or copying the value. We change its key and link it back in. Renaming a list with a million elements is O(1), with no copying, and it works for non-movable types like `SortedSet`.

```cpp
    if (expire_at != NO_EXPIRY) expiry_index_.insert({expire_at, to});
    return true;
}
```
The TTL travels with the key (the entry's `expire_at` was untouched). Only the index entry needs the new name.

```cpp
bool Database::set_expire(const std::string& key, int64_t at_ms) {
    Entry* entry = find(key);
    if (entry == nullptr) return false;
    if (at_ms <= clock_()) {
        remove(key);
        return true;
    }
```
A time in the past (e.g. `EXPIRE k -1`) deletes immediately. That's what Redis does, and it means we never store already-dead entries.

```cpp
    if (entry->expire_at != NO_EXPIRY) expiry_index_.erase({entry->expire_at, key});
    entry->expire_at = at_ms;
    expiry_index_.insert({at_ms, key});
    return true;
}
```
Replace any old index entry with the new one, keeping the entry and the index in sync.

```cpp
bool Database::persist(const std::string& key) {
    Entry* entry = find(key);
    if (entry == nullptr || entry->expire_at == NO_EXPIRY) return false;
    expiry_index_.erase({entry->expire_at, key});
    entry->expire_at = NO_EXPIRY;
    return true;
}
```
`PERSIST` removes a TTL. It returns whether there was one (that's what Redis replies with).

```cpp
int Database::remove_expired(int max_keys) {
    int64_t now = clock_();
    int removed = 0;
    while (removed < max_keys && !expiry_index_.empty()) {
        auto first = expiry_index_.begin();
        if (first->first > now) break;
        std::string key = first->second;
        remove(key);
        removed++;
    }
    return removed;
}
```
**Active expiry.** Look at the soonest-expiring key: if it isn't expired yet, *no* key is (the set is sorted), so stop. Otherwise delete it and look again.
- `std::string key = first->second;` **copies** the key, because `remove()` erases the set element that `first` points to. Using `first->second` after that would read freed memory. This is exactly the kind of bug AddressSanitizer catches.
- `max_keys` caps the work per call so the event loop never stalls.

```cpp
void Database::clear() {
    data_.clear();
    expiry_index_.clear();
}
```
`FLUSHALL`: both containers, always together.

```cpp
std::vector<std::string> Database::keys() const {
    std::vector<std::string> result;
    result.reserve(data_.size());
    for (const auto& [key, entry] : data_) {
        if (!is_expired(entry)) result.push_back(key);
    }
    return result;
}
```
`reserve` allocates once instead of growing repeatedly. `auto& [key, entry]` is a C++17 **structured binding**: it unpacks the map's `pair` into two named variables. It's `const`, so it skips expired keys instead of deleting them, because you can't modify a container while iterating it. Active expiry will clean them up soon.
