# Layer 0: A learning path for this project

This is a plan for going from "I've seen this repo" to "I can explain and defend every line of it."
It tells you **what** to study, **why** it matters, **how** to study it (what to read, run and break), and **how to check** you've really got it.

The other docs are the textbook. This is the syllabus.

Contents

1. [How to study: the PRBE loop](#1-how-to-study-the-prbe-loop)
2. [Pick a schedule](#2-pick-a-schedule)
3. [Phase 0: Play with it (1 hour)](#phase-0-play-with-it)
4. [Phase 1: The concepts (2-3 hours)](#phase-1-the-concepts)
5. [Phase 2: The modules, bottom-up](#phase-2-the-modules-bottom-up)
   [M1 Protocol](#m1-protocol) ·
   [M2 Storage and expiry](#m2-storage-and-expiry) ·
   [M3 Skip list and sorted set](#m3-skip-list-and-sorted-set) ·
   [M4 Commands and dispatch](#m4-commands-and-dispatch) ·
   [M5 Persistence (AOF)](#m5-persistence-aof) ·
   [M6 Networking and the event loop](#m6-networking-and-the-event-loop) ·
   [M7 The client](#m7-the-client) ·
   [M8 Tests, build, CI](#m8-tests-build-ci)
6. [Phase 3: See the whole system move](#phase-3-see-the-whole-system-move)
7. [Phase 4: Extend it (graded exercises)](#phase-4-extend-it)
8. [Phase 5: Defend it](#phase-5-defend-it)
9. [Mastery checklist](#mastery-checklist)

---

## 1. How to study: the PRBE loop

For every module, do these four steps in order. Reading alone gives you the *feeling* of understanding; the other three give you the real thing.

| Step | Do this | Why |
|---|---|---|
| **P**redict | Before opening the file, write 2-3 lines: "I expect this module to do X, and I think it handles Y by ...". | It forces you to make a model in your head, so the code either confirms or corrects it. Corrections are what you remember. |
| **R**ead | Open the source and its [walkthrough](3-walkthrough/README.md) side by side. Look up any unfamiliar C++ in [Layer 6](6-cpp-concepts.md). | The walkthrough explains every line; the C++ guide explains every feature. |
| **B**reak | Make the specific change suggested below, run the tests, and watch what fails. Then `git checkout -- .` to undo it. | You learn why a line exists by seeing what goes wrong without it. Every "break it" below has been checked to fail the way it says. |
| **E**xplain | Answer the checkpoint questions **out loud** or in writing, *without looking*. Then open the answers. | If you can't explain it simply, you don't own it yet. This is exactly what an interview tests. |

Keep a `notes.md` with your predictions, surprises and answers. Re-read it before any interview.

> **Undo everything with** `git checkout -- . && make clean`. You can't break the repo permanently.
> **Faster loop for breaking things**: `make SAN=1 unit-test`. The sanitizers turn silent memory bugs into loud errors at the exact line.

---

## 2. Pick a schedule

| Plan | Time | Covers | Good for |
|---|---|---|---|
| **Crash course** | 1 day (~7 h) | Phase 0, Phase 1, read [Layer 2](2-architecture.md) + [diagrams](5-diagrams.md), M1, M6, then [Layer 4](4-presenting.md) + [Layer 7](7-defense-guide.md) | an interview tomorrow |
| **Solid** | 1 week (~1.5 h/day) | Phases 0-3 and 5, every module with PRBE | being able to defend it confidently |
| **Deep** | 3 weeks | everything, plus 3-4 Phase 4 exercises | truly owning it, and having new things to talk about |

Suggested week:

| Day | Work |
|---|---|
| 1 | Phase 0 + Phase 1 |
| 2 | M1 Protocol + M2 Storage |
| 3 | M3 Skip list (the hardest; take your time) |
| 4 | M4 Commands + M5 AOF |
| 5 | M6 Networking + M7 Client |
| 6 | M8 + Phase 3 (strace, gdb) |
| 7 | Phase 5: mock interview, mastery checklist |

---

## Phase 0: Play with it

**What**: use the program before reading any code.
**Why**: it's much easier to read code when you've already seen what it does. You'll also collect questions ("why did that happen?") that the code will answer.
**How**:

```bash
make                                   # build
./bin/mini-redis-server                # terminal 1
./bin/mini-redis-cli                   # terminal 2
```

Try all of this in the CLI, and **predict each reply before pressing Enter**:

```
SET name "Alice Smith"
GET name
TYPE name
RPUSH queue a b c
LRANGE queue 0 -1
LPOP queue
GET queue                       # what will this say? why?
HSET user:1 name Bob age 30
HGETALL user:1
ZADD board 300 carol 100 alice 200 bob
ZRANGE board 0 -1 WITHSCORES
ZRANK board carol
SET temp x EX 5
TTL temp                        # run it a few times, then wait 5 s
INCR visits
INCR visits
KEYS *
```

Then look under the hood:

```bash
cat -A appendonly.aof | head -30     # the AOF is just RESP; ^M$ is \r\n
printf 'PING\r\n' | nc -q1 localhost 6379        # an inline command, raw
redis-cli -p 6379 GET name           # the official client works too (if installed)
```

Stop the server with Ctrl+C, start it again, and `GET name`. The data survived. Look at `appendonly.aof` again: where did `SET temp x EX 5` go, and why does it look different?

**Checkpoint**
1. What did `GET queue` return, and why is that the right behaviour?
2. What does the AOF contain for `SET temp x EX 5`? Why isn't it stored that way?
3. Which commands did *not* appear in the AOF at all?

<details><summary>Answers</summary>

1. `(error) WRONGTYPE ...`. `queue` holds a list, and `GET` only works on strings. Each key has exactly one type (a `std::variant`).
2. `SET temp x` followed by `PEXPIREAT temp <unix-ms>`. A relative "5 seconds" replayed tomorrow would give the key a fresh 5 seconds; an absolute time means the same moment after any restart.
3. All the read-only ones (`GET`, `TYPE`, `LRANGE`, `HGETALL`, `ZRANGE`, `ZRANK`, `TTL`, `KEYS`). Only commands that change data set `ctx.dirty`, and only those are logged.
</details>

---

## Phase 1: The concepts

**What**: the ideas the code is built on: sockets, TCP as a byte stream, RESP, event loops, pipelining, expiry, AOF and fsync, skip lists, signals.
**Why**: the code is mostly a direct translation of these ideas. If an idea is fuzzy, the code will look like magic.
**How**: read [Layer 1: Basics](1-basics.md) fully. Then look at [diagrams 1, 2 and 6](5-diagrams.md).

**Checkpoint** (answer before opening)
1. Why can't the server treat each `read()` as one command?
2. In RESP, why does `$5` come *before* the bytes `hello`?
3. Why does a single-threaded server need non-blocking sockets?
4. What's the difference between `write()` and `fsync()`?
5. What does a "span" in the skip list count?
6. What is the only safe thing to do inside a signal handler, and why?

<details><summary>Answers</summary>

1. TCP is a byte stream with no message boundaries. One `read()` may return half a command or ten commands.
2. The length lets the reader (a) know exactly when the value is complete, and (b) take any bytes as the value, including `\r\n` (binary safety), without scanning for a terminator.
3. A blocking `read()` would freeze the only thread waiting for one client while all the others are ignored.
4. `write()` copies data into the kernel's page cache (survives a process crash, not a power loss). `fsync()` forces it onto the physical disk.
5. How many bottom-level (level-0) nodes a link jumps over. Adding spans along a search path gives a node's rank.
6. Set a `volatile sig_atomic_t` flag. A handler can interrupt the program in the middle of anything, including `malloc` or `printf`, so calling non-reentrant functions from it can deadlock or corrupt state.
</details>

---

## Phase 2: The modules, bottom-up

Study in this order: each module only depends on the ones before it. Each one has **What / Why / How**, then **Read**, **Run**, **Break**, and a **Checkpoint**.

### M1 Protocol

- **What**: `resp_parser` turns bytes into a list of arguments. `resp_writer` turns replies into bytes.
- **Why**: this is the boundary with the outside world. Every byte from any client goes through `parse_command`, so it must handle partial data, pipelined data and hostile data.
- **How**: a stateless function returns `Ok` (with bytes consumed), `Incomplete` (consumed nothing) or `Error`. Bulk strings are read by length, never searched.
- **Read**: `server/resp_parser.h/.cpp`, `server/resp_writer.h/.cpp`; [walkthrough 01](3-walkthrough/01-protocol.md); [diagram 10](5-diagrams.md#10-parser-decision-flow).
- **C++ to look up**: [`enum class`](6-cpp-concepts.md#enum-class), [`std::string` as a byte buffer](6-cpp-concepts.md#stdstring-as-a-byte-buffer), [anonymous namespaces](6-cpp-concepts.md#anonymous-namespaces), [`to_chars`](6-cpp-concepts.md#stdfrom_chars--stdto_chars-c17).
- **Run**: `make unit-test` and read `tests/test_resp_parser.cpp`. Notice `every_partial_prefix_is_incomplete` tries *every* cut point.
- **Break (thought experiment first)**: in `parse_number`, the line `if (value > MAX_BULK_LENGTH) return false;` does two jobs. Delete it in your head: what happens with `$99999999999\r\n` (a client *claiming* a 100 GB argument)? And with a 25-digit number? Then try it, and send those bytes with `printf ... | nc localhost 6379`.

**Checkpoint**
1. Why does `parse_command` set `consumed = 0` and clear `args` on `Incomplete`?
2. A 1 MB value arrives in 64 chunks. How many times are its *bytes* scanned?
3. Why are there size limits, and what happens when one is exceeded? (And what second job does the `MAX_BULK_LENGTH` check inside `parse_number` do?)
4. Why does `format_double` use `std::to_chars` and not `printf("%.17g")`?

<details><summary>Answers</summary>

1. So the caller can simply retry from the same position when more bytes arrive. There's no half-parsed state anywhere. "Never hand out half a command."
2. Zero. The parser checks `pos + bulk_len + 2 > len` (is it all here yet?) without looking at the bytes. Only headers like `$1048576` are re-read on each attempt.
3. To stop a client making the server allocate unbounded memory (e.g. claiming a 100 GB argument and dribbling bytes). The parser returns `Error`; the server replies `-ERR Protocol error: ...` and closes the connection. Inside `parse_number` the check also stops `value * 10 + digit` from overflowing a `long long` on a very long digit string, which would be undefined behaviour.
4. `to_chars` produces the *shortest* text that reads back as exactly the same double (`1.1` → `"1.1"`, not `1.1000000000000001`), so ZSCORE replies are clean and AOF rewrites are exact.
</details>

### M2 Storage and expiry

- **What**: `Database` is the keyspace: `unordered_map<string, Entry>` plus a time-sorted expiry index.
- **Why**: everything else is built on it. Its rules (one type per key, TTL bookkeeping) are what make commands correct.
- **How**: `Entry` = `std::variant` value + absolute expiry time. `find()` does lazy expiry; `remove_expired()` does active expiry from `std::set<pair<time, key>>`. Tests swap in a fake clock.
- **Read**: `server/database.h/.cpp`, `server/clock.*`; [walkthrough 02](3-walkthrough/02-database.md); [diagrams 5 and 14](5-diagrams.md#5-what-one-key-looks-like-in-memory).
- **C++**: [`std::variant`](6-cpp-concepts.md#stdvariant-c17), [`std::set` and `std::pair`](6-cpp-concepts.md#stdset-and-stdpair), [`extract`](6-cpp-concepts.md#map-node-handles-extract-c17), [function pointers](6-cpp-concepts.md#function-pointers), [non-movable types](6-cpp-concepts.md#a-surprise-these-types-cannot-even-be-moved).
- **Run**: read `tests/test_database.cpp`. Find how the tests "travel in time" without sleeping.
- **Break**: in `Database::create`, delete the line `remove(key);`. Run the tests. Which test fails, and what real bug would users see? (Hint: `SET k v EX 10` followed by `SET k v2`.)
- **Measure**: run this to see the cost of a `std::variant` sized by its biggest member:
  ```cpp
  // save as /tmp/size.cpp, then: g++ -std=c++17 -I server /tmp/size.cpp && ./a.out
  #include <cstdio>
  #include "database.h"
  int main() { printf("Value=%zu Entry=%zu\n", sizeof(Value), sizeof(Entry)); }
  ```
  You'll get about 5088. Now read [known issue #1](7-defense-guide.md#known-issue-1-every-key-costs-about-5-kb). This is the most valuable thing to *discover yourself* in the whole project.

**Checkpoint**
1. Why both lazy and active expiry? What would go wrong with only one?
2. Why does the expiry index store `(time, key)` and not just `time`?
3. Why are expiry times absolute wall-clock milliseconds?
4. Why can `rename` move a 1-million-element list in O(1)?
5. Why is every key about 5 KB, and how would you fix it?

<details><summary>Answers</summary>

1. Lazy only: keys nobody reads stay in memory forever. Active only: a command could see a key that expired since the last sweep. Together: correct *and* memory gets reclaimed.
2. Two keys can expire in the same millisecond. A set needs unique elements, and when deleting a key you need to find *its* entry. `(time, key)` is unique and findable.
3. They're written to the AOF and must mean the same instant after a restart. A monotonic clock restarts from an arbitrary point each boot.
4. `extract()` unlinks the map node, the key is changed, and the node is re-inserted. The value never moves.
5. `std::variant` reserves space for its largest alternative, `SortedSet`, which contains a ~5 KB `std::mt19937`. Share one generator (a function-local `static`), or store `SortedSet` behind a `unique_ptr`. Measured: 505 MB → 21 MB for 100k keys.
</details>

### M3 Skip list and sorted set

- **What**: `SkipList` keeps `(score, member)` sorted with O(log n) insert, remove, rank and range. `SortedSet` pairs it with a hash map for O(1) score lookup.
- **Why**: `ZRANK`/`ZRANGE` need *positions*. A balanced tree can't give a position faster than O(n); spans can.
- **How**: random node heights (p = 1/4, max 32). The `update[]` and `rank[]` arrays record the search path; spans are fixed up along it.
- **Read**: `server/skiplist.*`, `server/sorted_set.*`; [walkthrough 03](3-walkthrough/03-skiplist.md); [diagrams 19-20](5-diagrams.md#19-skip-list-with-spans-a-worked-example).
- **Do by hand** (this is the most important exercise in the project): draw a 5-node skip list on paper with spans. Insert a node of height 2 in the middle, and update every span using the formulas in `insert`:
  `node.span = update[i].span - (rank[0] - rank[i])` and `update[i].span = (rank[0] - rank[i]) + 1`.
  Then compute `rank()` of the last node by adding spans along the path. Check against [diagram 19](5-diagrams.md#19-skip-list-with-spans-a-worked-example).
- **Run**: read `sorted_set_matches_reference_model`. Why compare against `std::set`?
- **Break** (both verified):
  1. In `insert`, delete the step-4 loop body `update[i]->levels[i].span++;`. Run `make unit-test`: `sorted_set_basic_operations` and `sorted_set_matches_reference_model` fail. The structure looks fine, but ranks are wrong.
  2. In `~SkipList`, replace the loop body with `delete node; node = node->levels[0].next;`. Run `make SAN=1 unit-test`: ASan reports **heap-use-after-free**. That's why `next` is saved first.
  3. In `remove`, delete `delete x;`. Run `make SAN=1 unit-test`: LeakSanitizer reports about 1 MB leaked in ~26 000 allocations.

**Checkpoint**
1. Why is a node's height random? What would a fixed pattern risk?
2. What do `update[i]` and `rank[i]` hold during `insert`?
3. Why must the skip list order by `(score, member)` and not just `score`?
4. Why does `SkipList` delete its copy constructor? What else did that (accidentally) delete?
5. Worst case of a skip list operation? Why isn't it a concern?

<details><summary>Answers</summary>

1. Random heights give O(log n) *expected* structure for any insertion order, with no rebalancing. A deterministic pattern can be degraded by an unlucky order of inserts and deletes.
2. `update[i]`: the last node on level `i` before the insertion point. `rank[i]`: that node's position (how many level-0 steps from the head).
3. Many members can share a score. Ordering ties by member makes the order total and deterministic, and lets `remove(score, member)` find the exact node.
4. It owns raw pointers; a copy would share nodes and double-free them. Declaring the copy constructor also suppresses the implicit move constructor, so `SkipList`, `SortedSet`, `Value` and `Entry` are all non-movable. It still works because node-based maps never move their elements and values are built in place.
5. O(n), if the random levels were all 1. The chance of that drops exponentially with n, so it's not a practical concern.
</details>

### M4 Commands and dispatch

- **What**: `execute_command` looks up the name, checks arity, runs the handler, and logs to the AOF if data changed. 53 handlers live in five `cmd_*.cpp` files.
- **Why**: one central place for validation means no handler can index past `args`. One file per type keeps each readable top to bottom.
- **How**: a `static` `unordered_map<string, {function pointer, arity}>`, built once. `find_typed<T>` / `find_or_create<T>` give type-checked access.
- **Read**: `server/commands.*`, `server/command_helpers.*`, `server/glob.cpp`, then **one** `cmd_*.cpp` fully (start with `cmd_strings.cpp`); [walkthrough 04](3-walkthrough/04-commands.md); [diagram 11](5-diagrams.md#11-command-dispatch); [Reference §1](8-reference.md#1-commands).
- **C++**: [templates](6-cpp-concepts.md#function-templates), [lambdas](6-cpp-concepts.md#lambdas), [magic statics](6-cpp-concepts.md#function-local-static-magic-statics), [signed overflow](6-cpp-concepts.md#signed-integer-overflow).
- **Break** (both verified):
  1. In `cmd_lists.cpp` `push_generic`, delete `ctx.dirty = true;`. `make unit-test`: `aof_replay_restores_all_types` fails. The command works in memory, but the list is gone after a restart.
  2. In `cmd_strings.cpp` `incr_by`, change the overflow check to `if (false) {`. `make SAN=1 unit-test`: UBSan prints `signed integer overflow: 1 + 9223372036854775807 cannot be represented in type 'long long int'` and the `counters` test fails.

**Checkpoint**
1. What does arity `-3` mean? Give a command that uses it.
2. When is `ctx.dirty` *not* set even though the command "ran"?
3. Why does `ZADD` validate all scores before adding any?
4. Why does `LPOP` delete the key when the list becomes empty?
5. How does `find_typed<T>` distinguish "missing" from "wrong type"?

<details><summary>Answers</summary>

1. At least 3 arguments including the name. `SET key value [options]`, `LPUSH key v1 [v2 ...]`.
2. When nothing changed: `DEL missing`, `SET k v NX` on an existing key, `LREM` that removed nothing, `EXPIRE` on a missing key, and every read-only command.
3. So a bad score halfway through doesn't leave the command half-applied. All or nothing.
4. To match Redis: empty containers don't exist. Otherwise `EXISTS`, `TYPE` and `KEYS` would report keys with nothing in them.
5. It returns `nullptr` in both cases but sets `wrong_type = true` only when the key exists with another type (`std::get_if` returned null).
</details>

### M5 Persistence (AOF)

- **What**: log data-changing commands in RESP, replay them at startup, compact on demand.
- **Why**: RAM disappears on restart. A log reuses the parser and the commands, so there's no second code path to get wrong.
- **How**: buffer in memory → `flush()` after each batch, *before* replies → fsync by policy. TTLs are made absolute. Truncated tails are repaired; garbage in the middle is refused. Rewrite = temp file + fsync + atomic `rename`.
- **Read**: `server/aof.*`; [walkthrough 05](3-walkthrough/05-aof.md); [diagrams 15-17](5-diagrams.md#15-aof-write-path-and-fsync-timing); [Reference §3](8-reference.md#3-what-gets-written-to-the-aof).
- **Run** (crash recovery, by hand):
  ```bash
  ./bin/mini-redis-server --aof-file /tmp/t.aof &        # start
  ./bin/mini-redis-cli SET a 1
  kill %1                                                # stop cleanly
  printf '*3\r\n$3\r\nSET\r\n$1\r\nb' >> /tmp/t.aof      # fake a crash mid-write
  ./bin/mini-redis-server --aof-file /tmp/t.aof          # watch: "removing ... bytes of incomplete command"
  ```
  Then put garbage in the *middle* of the file and restart: the server refuses with the byte offset.
- **Break**: in `Aof::rewrite`, delete the three lines after the `rename` (closing and reopening `fd_`). Run `make unit-test`. Does `aof_rewrite_compacts_the_file` notice? Why? (Writes after the rewrite go to the old, deleted file.)

**Checkpoint**
1. Why is the AOF flushed *before* replies are sent?
2. A crash cuts the last command in half. Why is it safe to just drop it?
3. Why is `rename()` the key to a safe rewrite?
4. Why must the fd be reopened after the rewrite?
5. What does each fsync policy lose on (a) a process crash, (b) a power loss?

<details><summary>Answers</summary>

1. So a client is never told `OK` for a write that isn't in the log (at least in the kernel).
2. That command's reply was never sent (the flush comes before the reply), so no client was told it succeeded.
3. It's atomic: anyone sees either the complete old file or the complete new file. A crash at any moment leaves a valid AOF.
4. The old fd still points at the old file, which `rename` unlinked. Writes would go to a file no one can open, and be lost.
5. (a) Nothing, in all three: the data is already in the page cache. (b) `always`: about nothing; `everysec`: up to ~1 s; `no`: up to ~30 s.
</details>

### M6 Networking and the event loop

- **What**: `Server` runs one `epoll` loop over non-blocking sockets, with per-connection input and output buffers.
- **Why**: thousands of clients, one thread, no locks, and every command atomic.
- **How**: level-triggered; one 16 KB `read` per event; parse all complete commands; flush AOF; send; enable `EPOLLOUT` only while output is pending; background jobs every loop; signal sets a flag.
- **Read**: `server/net.*`, `server/connection.h`, `server/server.*`, `server/main.cpp`, `server/config.h`; [walkthrough 06](3-walkthrough/06-server.md); [diagrams 6-9, 12, 13, 18](5-diagrams.md#6-the-event-loop-one-iteration).
- **C++/POSIX**: [errno, EINTR, EAGAIN](6-cpp-concepts.md#errno-eintr-eagain), [epoll](6-cpp-concepts.md#epoll), [signals](6-cpp-concepts.md#signals-and-volatile-sig_atomic_t), [partial writes](6-cpp-concepts.md#partial-reads-and-writes), [use-after-free traps](6-cpp-concepts.md#use-after-free-traps-the-code-avoids).
- **Run**: the [Phase 3 strace experiment](#phase-3-see-the-whole-system-move).
- **Break**: in `on_writable`, change `watch_for_writes(conn, !all_sent);` to `watch_for_writes(conn, true);`. Start the server, connect one client that sends one `PING` and then idles, and run `top`. The server spins at (close to) 100% CPU while doing nothing; measured ~80% averaged over its whole lifetime, idle startup included. Explain why.

**Checkpoint**
1. Why only one `read()` per readable event?
2. What happens if a client sends half a command and then waits?
3. Why is `EPOLLOUT` switched on and off?
4. Why do `on_readable`/`on_writable` return `bool`?
5. Why use `MSG_NOSIGNAL` *and* ignore `SIGPIPE`?
6. How does Ctrl+C lead to a clean shutdown? Trace every step.

<details><summary>Answers</summary>

1. Fairness. A client streaming lots of data gets 16 KB per turn like everyone else. Level-triggered epoll reports the socket again if more is waiting.
2. The bytes sit in `conn.input`. `parse_command` returns `Incomplete`, nothing is consumed, and the loop serves others. When the rest arrives, parsing restarts at the command's beginning.
3. Sockets are almost always writable; with level triggering, a permanent `EPOLLOUT` would wake the loop continuously (100% CPU). It's needed only while unsent output exists.
4. `false` means "the connection was closed and the `Connection` object destroyed", so the caller must not touch its reference again.
5. Defence in depth. `MSG_NOSIGNAL` covers `send()`; ignoring `SIGPIPE` covers any other write path. One disconnecting client must never kill the server.
6. SIGINT → handler sets `g_stop_requested = 1` → `epoll_wait` returns `EINTR` (no `SA_RESTART`) → `continue` → the `while` condition is false → `shutdown()`: close every fd, flush and fsync the AOF, print "Bye".
</details>

### M7 The client

- **What**: a blocking client with a REPL, one-shot mode and pipe mode.
- **Why**: the client does one thing at a time, so blocking calls are the simplest correct design.
- **How**: `getaddrinfo` (IPv4/IPv6) → `split_args` (quotes, escapes) → `encode_command` → `send_all` → `ReplyReader` (16 KB buffered, recursive) → `format_reply` (redis-cli style).
- **Read**: `client/*`; [walkthrough 07](3-walkthrough/07-client.md); [diagram 21](5-diagrams.md#21-client-architecture).
- **Run**: `echo 'SET a "x y"' | ./bin/mini-redis-cli` vs `./bin/mini-redis-cli SET a "x y"`. Why does each work? Try `./bin/mini-redis-cli GET` (wrong arity) then `echo $?`.

**Checkpoint**
1. Why does the REPL check `isatty(STDIN_FILENO)`?
2. Why is `read_reply` recursive?
3. Why does one-shot mode exit with code 1 on an error reply?

<details><summary>Answers</summary>

1. To show a prompt only for a human. Piped input shouldn't produce prompts mixed into the output.
2. RESP arrays can contain arrays. The same function reads each element, at any depth.
3. So shell scripts can use it in conditions: `mini-redis-cli SET k v || echo failed`.
</details>

### M8 Tests, build, CI

- **What**: a 77-line test framework, 31 unit tests, a shell e2e suite, micro benchmarks, a Makefile with a sanitizer build, and GitHub Actions.
- **Why**: evidence. Every claim in the README is backed by a test you can point to.
- **Read**: `tests/test_framework.h`, `tests/test_main.cpp`, `tests/e2e_test.sh`, `Makefile`, `.github/workflows/ci.yml`; [walkthrough 08](3-walkthrough/08-tests-and-build.md); [diagrams 22-23](5-diagrams.md#22-build-graph).
- **C++**: [macros](6-cpp-concepts.md#macros-define), [`#` and `##`](6-cpp-concepts.md#stringizing--and-token-pasting-), [self-registering tests](6-cpp-concepts.md#self-registering-tests-static-initialization).
- **Run**: `make SAN=1 test`, `make microbench`, and (if `redis-server` is installed) `make benchmark`.

**Checkpoint**
1. How does a `TEST(...)` get run when `main` never mentions it?
2. Why is `all_tests()` a function with a `static` inside, not a global vector?
3. What does `-MMD -MP` do, and what goes wrong without it?
4. Why build sanitizer objects into a separate directory?

<details><summary>Answers</summary>

1. The macro defines a global `TestRegistrar` whose constructor (run before `main`) adds the test to `all_tests()`.
2. Globals in different files are initialised in an unspecified order. A function-local static is created on first use, so it exists whenever the first registrar needs it.
3. They make the compiler write header dependency files that `make` includes. Without them, editing a header doesn't rebuild the `.cpp` files using it, so you get stale objects with mismatched layouts.
4. So `-fsanitize` objects and normal `-O2` objects never get linked together.
</details>

---

## Phase 3: See the whole system move

**What**: watch the real program's system calls and step through it in a debugger.
**Why**: this connects all the modules into one picture, and it's an impressive thing to show in an interview.

### strace: the whole architecture in 16 lines

```bash
strace -e trace=epoll_wait,accept,read,write,sendto,fdatasync,epoll_ctl \
       ./bin/mini-redis-server --port 7000 --aof-file /tmp/s.aof
# other terminal:
./bin/mini-redis-cli -p 7000 SET name Alice      # then Ctrl+C the server after a second
```

Real output (the idle `epoll_wait(...) = 0` timeouts removed):

```
accept(4, NULL, NULL)                   = 6                      new client gets fd 6
epoll_ctl(5, EPOLL_CTL_ADD, 6, {events=EPOLLIN, ...}) = 0        watch it for reads
accept(4, NULL, NULL)                   = -1 EAGAIN              nobody else waiting
epoll_wait(5, [{events=EPOLLIN, data={u32=6, ...}}], 128, 100) = 1
read(6, "*3\r\n$3\r\nSET\r\n$4\r\nname\r\n$5\r\nAlice"..., 16384) = 34
write(3, "*3\r\n$3\r\nSET\r\n$4\r\nname\r\n$5\r\nAlice"..., 34) = 34   AOF FIRST
sendto(6, "+OK\r\n", 5, MSG_NOSIGNAL, NULL, 0) = 5                 THEN the reply
epoll_wait(5, [{events=EPOLLIN, data={u32=6, ...}}], 128, 100) = 1
read(6, "", 16384)                      = 0                      client hung up
epoll_ctl(5, EPOLL_CTL_DEL, 6, NULL)    = 0
fdatasync(3)                            = 0                      everysec tick
epoll_wait(5, ...) = -1 EINTR                                    Ctrl+C
write(1, "\nShutting down...\n", 18)    = 18
fdatasync(3)                            = 0                      final sync
write(1, "AOF flushed to disk\n", 20)   = 20
write(1, "Bye\n", 4)                    = 4
```

Point to each line and name the code that produced it. If you can do that, you understand the server.
Now try the same with `--appendfsync always`: where does `fdatasync` move to?

### gdb: stop inside a command

```bash
make SAN=0                                  # -g is always on, so symbols exist
gdb --args ./bin/mini-redis-server --port 7000 --no-aof
(gdb) break cmd_set                         # (anonymous namespace)::cmd_set
(gdb) run
# other terminal: ./bin/mini-redis-cli -p 7000 SET a b
(gdb) bt                                    # the full call stack: main -> run -> event_loop -> on_readable -> process_input -> execute_command -> cmd_set
(gdb) print args                            # the parsed arguments
(gdb) finish                                # run to the end of cmd_set
(gdb) print ctx.out                         # "+OK\r\n" is already in the output buffer
```

The `bt` output *is* [diagram 8](5-diagrams.md#8-one-request-end-to-end) as a call stack. (With `-O2` some variables show as `<optimized out>`. For a smoother session, temporarily change `-O2` to `-O0` in the Makefile.)

---

## Phase 4: Extend it

**What**: add features and fix the known issues.
**Why**: changing code is the real test of understanding, and each exercise gives you a new, true story for interviews ("I found X, measured it, fixed it").
**How**: for each one: write a failing test first, implement, `make SAN=1 test`, measure if relevant.

| # | Exercise | Difficulty | Touches | You learn |
|---|---|---|---|---|
| 1 | Add `GETDEL key` | ★ | `cmd_strings.cpp`, `test_commands.cpp` | the handler pattern, the dirty flag |
| 2 | Add `ZREVRANGE` or `ZRANGE ... REV` | ★ | `cmd_zsets.cpp` | rank arithmetic |
| 3 | **Fix known issue #1** (5 KB per key) | ★ | `skiplist.h/.cpp` | `sizeof`, variants, measuring RSS. Expect 505 MB → 21 MB |
| 4 | Add `LPOP key count` | ★★ | `cmd_lists.cpp` | optional arguments, array replies |
| 5 | Make the inline parser support `"quoted strings"` | ★★ | `resp_parser.cpp` | parsing, reuse ideas from `arg_splitter.cpp` |
| 6 | Output-buffer limit (known issue #2) | ★★ | `server.cpp`, `connection.h` | backpressure, test with a client that never reads |
| 7 | Log `SET ... PXAT` as one record (known issue #4) | ★★ | `cmd_strings.cpp`, `aof.cpp` | AOF atomicity |
| 8 | Add `ZINCRBY` | ★★ | `sorted_set.*`, `cmd_zsets.cpp` | keeping two structures in sync |
| 9 | Report AOF write errors with `-MISCONF` (known issue #3) | ★★ | `aof.cpp`, `commands.cpp` | durability guarantees |
| 10 | Add a **set** type (`SADD SREM SISMEMBER SMEMBERS SCARD`) | ★★★ | `database.h` (variant), new `cmd_sets.cpp`, `aof.cpp` rewrite | how a new type flows through every layer |
| 11 | `MULTI` / `EXEC` / `DISCARD` | ★★★ | `connection.h`, `server.cpp`, `commands.cpp` | why single-threading makes transactions easy |
| 12 | `SCAN cursor [MATCH p] [COUNT n]` | ★★★ | `cmd_keys.cpp` | incremental iteration over a changing hash table |
| 13 | Background rewrite with `fork()` | ★★★★ | `aof.cpp`, `server.cpp` | copy-on-write, `waitpid`, rewrite buffers |
| 14 | Pub/Sub (`SUBSCRIBE`, `PUBLISH`) | ★★★★ | `server.cpp`, `connection.h` | server-pushed messages, connection modes |

Start with #3: it's tiny, measurable, and the best interview story in the project.

---

## Phase 5: Defend it

**What**: practise explaining and defending the project under questioning.
**How**:
1. Read [Layer 4](4-presenting.md) (pitch, standard Q&A) and [Layer 7](7-defense-guide.md) (evidence, dossiers, known issues).
2. **Whiteboard drill**: from memory, draw [diagram 2](5-diagrams.md#2-layered-architecture) (layers), [diagram 8](5-diagrams.md#8-one-request-end-to-end) (one request) and [diagram 19](5-diagrams.md#19-skip-list-with-spans-a-worked-example) (spans). Compare with the originals.
3. **Mock interview**: have someone pick random questions from Layer 4 §"Likely interview questions" and Layer 7 §6. Answer each using the [5-part shape](7-defense-guide.md#1-the-5-part-answer): what, why, how, evidence, cost. Time yourself: aim for 60-90 seconds each.
4. **Live demo**: run the [demo script](4-presenting.md#demo-script-2-minutes-live) until you can do it without notes, including the `strace` view.
5. **Weakness drill**: explain known issues #1-#3 and their fixes without looking.

---

## Mastery checklist

You own this project when you can do all of these **without notes**:

**Explain**
- [ ] The path of `SET name Alice` from keypress to printed `OK`, naming every function.
- [ ] Why TCP needs framing and how RESP's length prefix provides it.
- [ ] How one thread serves thousands of clients, and what would freeze it.
- [ ] Level vs edge triggering, and why `EPOLLOUT` is toggled.
- [ ] Lazy vs active expiry, and why the index is `set<pair<time, key>>`.
- [ ] The skip list: random levels, spans, how `rank()` adds them up.
- [ ] What `write`, `fsync` and `rename` each guarantee, and the three fsync policies.
- [ ] How a truncated AOF is repaired and why a corrupt one is refused.
- [ ] The signal-handling pattern and why it's the only safe one.

**C++**
- [ ] `std::variant`, `get_if`, `emplace`, and the size of a variant.
- [ ] Why `SkipList` is non-copyable, and why that made it non-movable.
- [ ] RAII: name four classes that release resources in their destructors.
- [ ] Why `find_typed` is a template and why it lives in a header.
- [ ] Two places the code avoids undefined behaviour, and how.
- [ ] How `TEST(...)` registers itself before `main`.

**Do**
- [ ] Build, run all tests, run under sanitizers, run the micro benchmarks.
- [ ] Simulate a crash mid-write and show recovery.
- [ ] Show the AOF-before-reply ordering with `strace`.
- [ ] Measure memory per key, and explain the 5 KB.
- [ ] Add a new command with a test in under 20 minutes.

**Defend**
- [ ] Give the cost of every major decision, and when you'd change it.
- [ ] List five things the project does *not* do that real Redis does.
- [ ] Explain three known issues and their fixes.

---

Next: [Layer 1: The basics](1-basics.md) · Back to the [documentation hub](README.md)
