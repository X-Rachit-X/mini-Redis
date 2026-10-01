# 04: Commands: dispatch, helpers and the `cmd_*.cpp` files

There are 53 commands, but they follow a handful of patterns. This page explains the dispatch machinery line by line, then one handler of each pattern line by line. After that, the other handlers are variations you can read on your own. Every unusual line in them is listed.

---

## `server/commands.h`

```cpp
class Aof;
class Database;
```
**Forward declarations**: "these classes exist". The header only uses them by reference or pointer, so it doesn't need their full definitions. That keeps compile times down and avoids circular includes (`aof.h` includes `commands.h`).

```cpp
using Args = std::vector<std::string>;
```
A command and its arguments: `args[0]` is the name.

```cpp
struct CommandContext {
    Database& db;
    Aof* aof;
    std::string& out;
    bool dirty = false;
};
```
Everything a handler needs, bundled so every handler has the same signature.
- `db`: the data. It's a reference because it always exists.
- `aof`: a pointer because it may be absent (AOF disabled, or replay in progress), so `nullptr` is meaningful. Only `REWRITEAOF` and `CONFIG` use it.
- `out`: where to append the reply.
- `dirty`: the handler sets it to `true` if it changed data. Real Redis has the same idea (`server.dirty`). It decides what goes into the AOF.

```cpp
using Handler = void (*)(CommandContext& ctx, const Args& args);
```
A **function pointer type**: "pointer to a function taking (CommandContext&, const Args&) and returning nothing". Each command is one such function.

```cpp
struct Command {
    Handler handler;
    int arity;
};
```
Arity uses Redis's convention: positive means exact, negative means "at least". `GET key` has arity 2, `DEL key [key...]` has −2.

```cpp
using CommandTable = std::unordered_map<std::string, Command>;
void register_key_commands(CommandTable& table);
...
void execute_command(Database& db, Aof* aof, const Args& args, std::string& out);
```
Each data-type file exports one `register_*` function. `execute_command` is the single entry point used by the server, the AOF replay and the tests.

---

## `server/commands.cpp`

```cpp
const CommandTable& command_table() {
    static const CommandTable table = [] {
        CommandTable t;
        register_key_commands(t);
        register_string_commands(t);
        register_list_commands(t);
        register_hash_commands(t);
        register_zset_commands(t);
        return t;
    }();
    return table;
}
```
- A **function-local `static`** is initialized the first time the function runs, and never again. C++11 guarantees this happens exactly once, even with threads.
- `[] { ... }()` is a **lambda that is called immediately** (note the final `()`). It's a neat way to run several statements to build a `const` value.
- Why not a global variable? Globals in different `.cpp` files are initialized in an unspecified order. If a test ran before the table existed, it would crash (the "static initialization order fiasco"). The function-local static avoids that.

```cpp
bool arity_ok(int arity, size_t argc) {
    if (arity > 0) return static_cast<int>(argc) == arity;
    return static_cast<int>(argc) >= -arity;
}
```
Implements the arity convention. `static_cast<int>` is an explicit conversion. Mixing signed `int` with unsigned `size_t` in a comparison triggers a compiler warning (and can be a real bug with negatives).

```cpp
void execute_command(Database& db, Aof* aof, const Args& args, std::string& out) {
    if (args.empty()) return;
    const CommandTable& table = command_table();
    auto it = table.find(to_upper(args[0]));
```
Command names are case-insensitive, so we upper-case before the lookup. This is one hash lookup, O(1). Your old code used a 30-branch `if/else if` chain, which is O(number of commands) string comparisons.

```cpp
    if (it == table.end()) {
        reply_error(out, "ERR unknown command '" + args[0] + "'");
        return;
    }
    const Command& command = it->second;
    if (!arity_ok(command.arity, args.size())) {
        reply_error(out, "ERR wrong number of arguments for '" + to_lower(args[0]) + "' command");
        return;
    }
```
Validation happens **once, here**, so every handler can safely index `args[1]`, `args[2]`, ... up to its arity without checking. The error texts match real Redis exactly.

```cpp
    CommandContext ctx{db, aof, out};
    command.handler(ctx, args);
    if (ctx.dirty && aof != nullptr) aof->log_command(db, args);
}
```
Build the context (aggregate initialization; `dirty` keeps its default `false`), call the handler through the pointer, then log to the AOF only if the handler really changed something.

---

## `server/command_helpers.h` / `.cpp`

```cpp
inline const char* const ERR_WRONGTYPE = "WRONGTYPE Operation against a key holding the wrong kind of value";
```
`inline` variables (C++17) can live in a header that's included by many `.cpp` files without "multiple definition" link errors. `const char* const` is a constant pointer to constant characters.

```cpp
const long long MAX_EXPIRE_AMOUNT = 1000000000000LL;
```
10¹² seconds × 1000 = 10¹⁵ ms, far below `LLONG_MAX` (9.2×10¹⁸). It guarantees `now + amount * 1000` can never overflow. Signed overflow is **undefined behaviour** in C++, which UBSan would flag.

```cpp
std::string to_upper(std::string text) {
    for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}
```
`text` is taken **by value** (a copy we're allowed to modify), and we return it. `toupper` must receive an `unsigned char` value: passing a negative `char` (bytes ≥ 128) is undefined behaviour. That's a classic C++ trap.

```cpp
bool parse_integer(const std::string& text, long long& out) {
    const char* begin = text.data();
    const char* end = begin + text.size();
    std::from_chars_result result = std::from_chars(begin, end, out);
    return result.ec == std::errc() && result.ptr == end;
}
```
`std::from_chars` (C++17) converts text to a number. It doesn't throw exceptions, doesn't depend on the locale, rejects overflow (`ec` is set), and tells us where it stopped. `ptr == end` makes it **strict**: `"12abc"` and `" 12"` are rejected. Your old code used `std::stoi` with exceptions, and `stoi("12abc")` returns 12 silently.

```cpp
bool parse_score(const std::string& text, double& out) {
    if (text == "+inf") { out = std::numeric_limits<double>::infinity(); return true; }
    ...
    return result.ec == std::errc() && result.ptr == end && !std::isnan(out);
}
```
Same for doubles. `from_chars` accepts `inf`/`-inf` but not `+inf`, which Redis allows, so we handle it explicitly. NaN is rejected because it can't be ordered (NaN < x and x < NaN are both false), which would break the skip list.

```cpp
bool normalize_range(long long& start, long long& stop, long long size) {
    if (start < 0) start += size;
    if (stop < 0) stop += size;
    if (start < 0) start = 0;
    if (stop >= size) stop = size - 1;
    return start <= stop && start < size;
}
```
Redis ranges are inclusive, and negative means "from the end" (−1 = last). Convert, then clamp to valid bounds. If the range is empty, return false. For size 5: `(0,-1)` → `(0,4)`; `(-2,100)` → `(3,4)`; `(5,10)` → empty. Shared by `LRANGE` and `ZRANGE`.

```cpp
template <typename T>
T* find_typed(Database& db, const std::string& key, bool& wrong_type) {
    wrong_type = false;
    Entry* entry = db.find(key);
    if (entry == nullptr) return nullptr;
    T* value = std::get_if<T>(&entry->value);
    if (value == nullptr) wrong_type = true;
    return value;
}
```
**The helper that makes every handler short.** It's a template: the compiler generates one version per type used (`find_typed<List>`, `find_typed<Hash>`, ...). `std::get_if<T>(&variant)` returns a pointer to the value if the variant currently holds a `T`, otherwise `nullptr`. That gives three distinct outcomes:

| Situation | Returns | wrong_type |
|---|---|---|
| key holds a T | pointer | false |
| key missing / expired | nullptr | false |
| key holds another type | nullptr | **true** |

Templates must be defined in the header, because the compiler needs the body wherever it's used.

```cpp
template <typename T>
T* find_or_create(Database& db, const std::string& key, bool& wrong_type) {
    T* value = find_typed<T>(db, key, wrong_type);
    if (value != nullptr || wrong_type) return value;
    Entry& entry = db.create(key);
    return &entry.value.emplace<T>();
}
```
For write commands like `LPUSH` on a new key: if it's missing, create an entry and switch its variant to an empty `T` **in place** (`emplace<T>()` returns a reference to the new object).

---

## `server/glob.cpp`: wildcard matching for `KEYS`

```cpp
size_t p = 0, t = 0;       // positions in pattern and text
size_t star = NONE;        // where the last '*' was in the pattern
size_t star_text = 0;      // where we were in the text at that moment
```
A greedy algorithm with backtracking to the last star. It's O(n·m) worst case and needs no recursion.

```cpp
while (t < text.size()) {
    if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) { p++; t++; }
```
Same character (or `?`, which matches anything): advance both.

```cpp
    else if (p < pattern.size() && pattern[p] == '*') { star = p++; star_text = t; }
```
A star: remember where it is, and first assume it matches **zero** characters (move the pattern on, keep the text where it is).

```cpp
    else if (star != NONE) { p = star + 1; t = ++star_text; }
```
A mismatch, but we saw a star earlier: undo, and let that star swallow one more character.

```cpp
    else return false;
}
while (p < pattern.size() && pattern[p] == '*') p++;
return p == pattern.size();
```
The text is used up. Leftover stars can match nothing. It's a match only if the whole pattern was used.

Example: `user:*` vs `user:42`. `u,s,e,r,:` match; `*` is recorded; `4` vs end of pattern fails, so backtrack (the star eats `4`); `2` likewise; text done; pattern done. Match.

---

## Pattern 1: a read command (`GET`)

```cpp
void cmd_get(CommandContext& ctx, const Args& args) {
    bool wrong_type;
    std::string* value = find_typed<std::string>(ctx.db, args[1], wrong_type);
    if (wrong_type) {
        reply_error(ctx.out, ERR_WRONGTYPE);
    } else if (value == nullptr) {
        reply_null(ctx.out);
    } else {
        reply_bulk(ctx.out, *value);
    }
}
```
1. Look the key up expecting a string. `args[1]` is safe because arity 2 was already checked.
2. Wrong type gives an error, missing gives nil, otherwise the value.
3. It doesn't set `dirty` (nothing changed), so it isn't logged.

`STRLEN`, `LLEN`, `HGET`, `HLEN`, `HEXISTS`, `ZSCORE`, `ZCARD`, `ZRANK`, `TYPE`, `EXISTS`, `TTL` are all this shape.

## Pattern 2: a write that may create the key (`LPUSH`/`RPUSH`)

```cpp
void push_generic(CommandContext& ctx, const Args& args, bool to_front) {
    bool wrong_type;
    List* list = find_or_create<List>(ctx.db, args[1], wrong_type);
    if (wrong_type) { reply_error(ctx.out, ERR_WRONGTYPE); return; }
    for (size_t i = 2; i < args.size(); i++) {
        if (to_front) list->push_front(args[i]); else list->push_back(args[i]);
    }
    ctx.dirty = true;
    reply_integer(ctx.out, static_cast<long long>(list->size()));
}
void cmd_lpush(CommandContext& ctx, const Args& args) { push_generic(ctx, args, true); }
void cmd_rpush(CommandContext& ctx, const Args& args) { push_generic(ctx, args, false); }
```
- Two commands share one function, with a `bool` saying which end. That avoids duplicated code.
- `find_or_create` returns a list we can modify directly. The pointer points into the database, so no copy and no "save back" are needed.
- `std::deque::push_front` is O(1).
- `dirty = true`, then reply with the new length (as Redis does).

`APPEND`, `HSET`, `ZADD`, `HINCRBY` follow this shape.

## Pattern 3: a write that may empty the key (`LPOP`/`RPOP`)

```cpp
    std::string value;
    if (from_front) {
        value = std::move(list->front());
        list->pop_front();
    } else { ... back ... }
    if (list->empty()) ctx.db.remove(args[1]);
    ctx.dirty = true;
    reply_bulk(ctx.out, value);
```
- `std::move` *steals* the string's memory instead of copying it, which is fine because the element is about to be removed anyway.
- **Empty containers are deleted.** In Redis, an empty list/hash/zset doesn't exist (`EXISTS` returns 0 and `TYPE` returns none). It also keeps memory clean and keeps the AOF rewrite from writing an `RPUSH key` with no values. `LREM`, `HDEL` and `ZREM` do the same.

## Pattern 4: options parsing (`SET`)

```cpp
for (size_t i = 3; i < args.size(); i++) {
    std::string option = to_upper(args[i]);
    if (option == "NX") only_if_missing = true;
    else if (option == "XX") only_if_exists = true;
    else if ((option == "EX" || option == "PX") && i + 1 < args.size()) {
        long long amount;
        if (!parse_integer(args[i + 1], amount) || amount <= 0 || amount > MAX_EXPIRE_AMOUNT) { ...error...; return; }
        expire_at = ctx.db.now() + (option == "EX" ? amount * 1000 : amount);
        i++;
    } else { reply_error(ctx.out, ERR_SYNTAX); return; }
}
```
Walk the optional arguments after `SET key value`. `EX`/`PX` take a number, so we check that one exists (`i + 1 < args.size()`) and skip it with `i++`. Every error returns **before anything is changed**, so a bad command has no side effects.

```cpp
bool exists = ctx.db.find(args[1]) != nullptr;
if ((only_if_missing && exists) || (only_if_exists && !exists)) { reply_null(ctx.out); return; }
```
`NX`/`XX` conditions. If the condition isn't met, reply nil and leave `dirty` false, so nothing is logged.

```cpp
Entry& entry = ctx.db.create(args[1]);
entry.value.emplace<std::string>(args[2]);
if (expire_at != NO_EXPIRY) ctx.db.set_expire(args[1], expire_at);
```
`create` wipes the old value and TTL, which is Redis semantics (plain `SET` removes a TTL). `SET` works on a key of *any* old type, which is why it doesn't use `find_typed`.

## Pattern 5: safe arithmetic (`INCR` family)

```cpp
void incr_by(CommandContext& ctx, const std::string& key, long long delta) {
    ...
    long long current = 0;
    if (value != nullptr && !parse_integer(*value, current)) { error; return; }
    if ((delta > 0 && current > LLONG_MAX - delta) || (delta < 0 && current < LLONG_MIN - delta)) {
        reply_error(ctx.out, "ERR increment or decrement would overflow");
        return;
    }
    current += delta;
    if (value != nullptr) *value = std::to_string(current);
    else ctx.db.create(key).value.emplace<std::string>(std::to_string(current));
```
- A missing key counts as 0 (Redis semantics).
- **The overflow check happens before adding**, rearranged so the check itself can't overflow: `current + delta > MAX` ⇔ `current > MAX - delta`.
- Updating in place (`*value = ...`) keeps an existing TTL. Creating a new key has no TTL.
- `DECRBY` rejects `LLONG_MIN` because negating it (`-delta`) would overflow.

## Pattern 6: expiry commands (`EXPIRE`, `PEXPIRE`, `PEXPIREAT`)

```cpp
void expire_generic(CommandContext& ctx, const Args& args, long long unit_ms, bool absolute) {
    ...
    int64_t at = absolute ? amount : ctx.db.now() + amount * unit_ms;
    if (!ctx.db.set_expire(args[1], at)) { reply_integer(ctx.out, 0); return; }
    ctx.dirty = true;
    reply_integer(ctx.out, 1);
}
```
One function for three commands. `EXPIRE` passes 1000 (seconds to ms), `PEXPIRE` passes 1, and `PEXPIREAT` passes an absolute time. `PEXPIREAT` matters most: it's what the AOF writes, so that replay restores the exact same deadline.

## Notable lines in the other handlers

| Handler | Line | Why |
|---|---|---|
| `cmd_del` | `if (ctx.db.find(args[i]) != nullptr && ctx.db.remove(args[i]))` | `find` first, so an expired-but-not-yet-removed key isn't counted as deleted |
| `cmd_ttl` | `(ms_left + 500) / 1000` | Rounds to the nearest second like Redis, so `EX 10` immediately shows 10, not 9 |
| `cmd_keys` | loops over `db.keys()` with `glob_match` | O(n) over all keys, which is why `KEYS` is discouraged in production Redis |
| `cmd_mset` | `if (args.size() % 2 == 0)` | name + pairs is always odd |
| `cmd_mget` | `reply_null` for missing **or** wrong type | Redis behaviour: MGET never errors |
| `cmd_lrem` | `it = list->erase(it)` | `erase` returns the next valid iterator. Using the old one would be undefined behaviour. |
| `cmd_lrem` | negative count walks backwards by index | Simpler and safer than reverse iterators (the old code's `.base()` trick) |
| `hash_set_pairs` | `insert_or_assign(...).second` | `true` if the field was new, so we can count new fields like Redis |
| `cmd_hmset` | same helper, replies `+OK` | HMSET is the old name with the old reply |
| `hash_dump` | `with_fields`, `with_values` flags | One function serves HKEYS, HVALS and HGETALL |
| `cmd_zadd` | parses **all** scores first | A bad score in the middle must not leave the command half-applied (atomicity) |
| `cmd_zrange` | `items.size() * (with_scores ? 2 : 1)` | The array header must state the exact element count |
| `cmd_ping` | arity −1 + manual check | `PING` and `PING msg` are both valid |
| `cmd_command` | empty array | `redis-cli` sends `COMMAND DOCS` on start; an empty answer keeps it happy |
| `cmd_config` | answers `save` / `appendonly` | `redis-benchmark` asks these at startup |
| `cmd_rewriteaof` | `ctx.aof == nullptr` check | No AOF means nothing to rewrite |

### Registration

```cpp
void register_list_commands(CommandTable& table) {
    table["LPUSH"] = {cmd_lpush, -3};
    ...
}
```
`{cmd_lpush, -3}` builds a `Command` (aggregate initialization). The handler functions are in an anonymous namespace (private to the file), but the *pointers* to them can be stored in the table and called from anywhere.
