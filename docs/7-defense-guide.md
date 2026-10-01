# Layer 7: Defending every decision

[Layer 4](4-presenting.md) gives you the pitch and the standard interview answers.
This layer goes further: for every claim you might make about this project, it gives you **the evidence**, **a way to prove it live**, **the alternatives you rejected**, and **the honest weaknesses**.

A claim you can't back up hurts you more than one you never made. A weakness you found and explained yourself helps you more than a perfect-sounding project.

Contents

1. [The 5-part answer](#1-the-5-part-answer)
2. [Verified facts (measured, not copied)](#2-verified-facts-measured-not-copied)
3. [Claim → evidence map](#3-claim--evidence-map)
4. [Decision dossiers: what, why, how, alternatives, cost](#4-decision-dossiers)
5. [Complexity of every operation](#5-complexity-of-every-operation)
6. [Hard follow-up questions](#6-hard-follow-up-questions)
7. [Known issues and limitations (find them before they find you)](#7-known-issues-and-limitations)
8. [Phrases to avoid, and what to say instead](#8-phrases-to-avoid-and-what-to-say-instead)

---

## 1. The 5-part answer

Any "why did you do X?" question can be answered with the same shape. Practise it until it's automatic.

| Part | Question it answers | Example (single-threaded event loop) |
|---|---|---|
| **What** | What does it do? | "One thread uses epoll to wait on every socket and runs each command to completion." |
| **Why** | What problem does it solve? | "Commands take microseconds, so the bottleneck is waiting on the network. epoll waits on all clients at once, and one thread needs no locks." |
| **How** | Where is it in the code? | "`Server::event_loop` in `server.cpp:86`. Sockets are non-blocking, and EPOLLOUT is enabled only while output is pending." |
| **Evidence** | How do you know it works? | "The e2e test runs 20 concurrent clients doing 25 INCRs each and checks the result is exactly 500. That's also run under ASan." |
| **Cost** | What did you give up? When would you change it? | "It uses one core, and one slow command (like `KEYS *`) blocks everyone. For more throughput I'd shard across instances, or add I/O threads like Redis 6." |

The **Cost** part is what separates a strong answer from a memorised one. Always volunteer it.

---

## 2. Verified facts (measured, not copied)

These numbers were reproduced by actually building and running the project (4-core Intel Xeon @ 2.8 GHz cloud VM, g++ 13, `-O2`).
**Re-measure on your own machine before quoting them**; timing results vary a lot between machines.

| Fact | Value | How to reproduce |
|---|---|---|
| Commands implemented | **53** | `grep -h 'table\["' server/cmd_*.cpp \| wc -l` |
| Unit tests | **31/31 pass** (also under ASan + UBSan) | `make test`, `make SAN=1 test` |
| End-to-end checks | **34**, plus **1** more when the official `redis-cli` is installed (35 total) | `make e2e-test` |
| Lines of C++ (server + client, incl. comments) | about 3 100 | `wc -l server/* client/*` |
| `LPUSH` ×50 000: vector vs deque | 5132 ms vs 1.5 ms (**~3500×**) | `make microbench` |
| RESP parse, 1M pipelined `SET`s | 9.2 M commands/s, ~410 MB/s | `make microbench` |
| `ZRANK` ×200 on 200 000 members: skip list vs `std::set` walk | 0.85 ms vs 3976 ms (**~4700×**) | `make microbench` |
| `sizeof(Value)` / `sizeof(Entry)` | **5088 / 5096 bytes** | see [known issue #1](#known-issue-1-every-key-costs-about-5-kb) |
| RSS after 100 000 `SET kN v` (no AOF) | **~505 MB** (~5 KB per key) | see [known issue #1](#known-issue-1-every-key-costs-about-5-kb) |
| Same, after the one-line fix | **~21 MB** (~170 B per key) | see [known issue #1](#known-issue-1-every-key-costs-about-5-kb) |

The README's micro benchmark figures (~1000×, ~39 M cmds/s, ~6000×) came from a different machine (WSL2). The *ratios* are the point: deque and the skip list win by three orders of magnitude on any machine. Quote your own measurements.

### Throughput vs real Redis (`redis-benchmark`)

Measured on the same 4-core VM: Redis 7.0.15 vs mini-redis, both single-threaded with persistence off, 50 000 requests per test, client on the same machine.

| Scenario | mini-redis as % of Redis (SET / GET / INCR / LPUSH / RPOP / HSET / ZADD) | mini-redis req/s range |
|---|---|---|
| 1 client, no pipeline | about 100% across the board (100-107%) | ~13-14 k |
| 1 client, pipeline 16 | 91-122% | ~160-235 k |
| 50 clients, no pipeline | 80-107% | ~48-60 k |
| 50 clients, pipeline 16 | 81-161% | ~650-770 k |

**How to read this honestly** (say this if you quote the numbers):
- It's the **same order of magnitude** as Redis, which is what a correct epoll design should give. It does **not** mean "faster than Redis". Results swing ±20% between runs on a shared VM, and the client competes with the server for the same 4 cores.
- `redis-benchmark` without `-r` uses **one single key** (`key:__rand_int__` literally). That's the best case: no hash-table growth and no memory pressure. It completely hides [known issue #1](#known-issue-1-every-key-costs-about-5-kb). Re-run with `-r 1000000` to touch a million keys and watch mini-redis's memory.
- Real Redis does extra work per command (statistics, keyspace notifications, client tracking, latency monitoring) that mini-redis skips.
- Where Redis wins (50 clients, no pipeline): Redis has years of tuning in its networking path.
- Reproduce: `make benchmark`, after the readiness-probe fix in [known issue #11](#known-issue-11-make-benchmark-can-hang-forever).

The single most defensible sentence: *"With the same redis-benchmark settings on the same machine, mini-redis is within roughly ±20% of Redis on simple commands, for a single hot key. I'd want to test with many keys before claiming more, and that test is where I found the 5 KB-per-key problem."*


---

## 3. Claim → evidence map

Every claim from the README, with where it lives and how to prove it in front of someone.

| Claim | Code | Automated evidence | Prove it live |
|---|---|---|---|
| Speaks real RESP; official tools work | `resp_parser.cpp`, `resp_writer.cpp` | e2e "compatibility with the official redis-cli" | `redis-cli -p 6379 SET a 1`, `redis-benchmark -t set,get -q` |
| Handles commands split across packets | `resp_parser.cpp:123` (returns `Incomplete`), `server.cpp:192` | `every_partial_prefix_is_incomplete` (tries **every** cut point); e2e "split across two TCP packets" | `exec 3<>/dev/tcp/127.0.0.1/6379; printf '*1\r\n$4\r\nPI' >&3; sleep 1; printf 'NG\r\n' >&3; head -c 7 <&3` |
| Pipelining | `server.cpp:187-201` (loop) | `parses_pipelined_commands_one_at_a_time`; e2e 1000 PINGs in one write | `printf 'PING\r\nPING\r\nPING\r\n' \| nc -q1 localhost 6379` |
| Binary-safe values | `resp_parser.cpp:120-129` (jumps by length, never scans) | `values_are_binary_safe` | `SET k "a\r\nb"` then `GET k` shows `"a\r\nb"` |
| Inline commands | `resp_parser.cpp:44-73` | `parses_inline_commands`; e2e "inline command" | `telnet localhost 6379`, type `PING` |
| Size limits stop abuse | `resp_parser.cpp:7-9`, `server.cpp:25, 167` | `rejects_malformed_input` | `printf '*99999999999\r\n' \| nc localhost 6379` gives `-ERR Protocol error` |
| Single-threaded, so commands are atomic | `server.cpp:86-124` (no threads anywhere) | e2e 20 clients × 25 `INCR` = exactly 500 | run 20 parallel `redis-benchmark -t incr` jobs, then `GET` the counter |
| One type per key, `WRONGTYPE` errors | `database.h:19` (`std::variant`), `command_helpers.h:39-47` | `wrong_type_is_reported` | `RPUSH l a` then `GET l` |
| Empty containers disappear | `cmd_lists.cpp:49`, `cmd_hashes.cpp:70`, `cmd_zsets.cpp:69` | `empty_containers_are_deleted` | `RPUSH l a`, `LPOP l`, `EXISTS l` gives 0 |
| Skip list with spans: O(log n) rank | `skiplist.cpp:112-128` | `sorted_set_matches_reference_model` (20 000 random ops vs `std::set`); microbench | `make microbench` |
| Lazy + active expiry | `database.cpp:22-30`, `88-100`; `server.cpp:252-257` | `lazy_expiry_hides_and_deletes_keys`, `active_expiry_removes_untouched_keys` (fake clock) | `SET k v PX 100`, wait, `DBSIZE` drops without touching `k` |
| AOF before reply | `server.cpp:175-179` | (ordering is by construction) | `strace -e write,sendto -p <pid>`: the AOF `write` comes before `sendto` |
| Relative TTL logged as absolute | `aof.cpp:83-105` | `aof_logs_absolute_expiry_times` | `SET k v EX 100`, then `cat appendonly.aof` shows `PEXPIREAT` |
| Truncated tail repaired | `aof.cpp:172-182` | `aof_truncated_tail_is_repaired` | `printf '*3\r\n$3\r\nSET' >> appendonly.aof`, restart, read the log line |
| Corrupt AOF refused | `aof.cpp:168-171` | `aof_corrupt_file_is_rejected` | put garbage in the middle, restart: "Refusing to start" |
| Atomic rewrite | `aof.cpp:121-150` | `aof_rewrite_compacts_the_file`; e2e "REWRITEAOF compacts" | `INCR c` ×1000, `ls -l`, `REWRITEAOF`, `ls -l` |
| Graceful shutdown | `server.cpp:16-42`, `264-279` | e2e `stop_server` sends SIGTERM, then restart checks data | Ctrl+C prints "AOF flushed to disk", "Bye" |
| Memory-safe | whole codebase | CI matrix `SAN=1` | `make SAN=1 test` |

---

## 4. Decision dossiers

For each important decision: **what** was chosen, **why**, **how** it's implemented, **alternatives rejected** and why, the **cost**, and **when you would change your mind**. That last part shows judgement, not just knowledge.

### 4.1 Single-threaded epoll loop
- **What**: one thread, non-blocking sockets, level-triggered `epoll`.
- **Why**: in-memory commands take ~1 µs. Network waiting dominates, and epoll waits on everyone at once. No locks means no deadlocks, no data races, and atomic commands for free.
- **How**: `server.cpp:86-124`.
- **Alternatives rejected**:
  - *Thread per client*: ~8 MB of reserved stack per thread, context switches, and a global mutex on the database would serialise commands anyway. All the cost of threads for none of the benefit.
  - *Thread pool + locks*: lock contention on a hot keyspace, and much harder to prove correct.
  - *`select`/`poll`*: O(n) per call over all fds; `select` is capped at 1024 fds.
  - *io_uring*: faster in theory, but newer, more complex, and not needed at this scale.
- **Cost**: one core. One slow command (`KEYS *` on 10M keys, a big `REWRITEAOF`) stalls every client.
- **Change it when**: CPU-bound on protocol work → add I/O threads for read/parse/write but keep execution single-threaded (Redis 6 design). Need more capacity → run several instances and shard keys between them (Redis Cluster idea).

### 4.2 Level-triggered, one `read()` per event
- **Why**: fairness. A client sending 1 GB can't hog the loop; it gets 16 KB per turn like everyone else. It's also forgiving: if you don't drain the socket, epoll simply reports it again.
- **Alternative**: edge-triggered (`EPOLLET`) with "read until `EAGAIN`". Fewer wake-ups, but forgetting to drain hangs a connection forever, and draining greedily is unfair to other clients.
- **Cost**: a few more `epoll_wait` calls under heavy load.

### 4.3 Stateless, re-parse-on-incomplete parser
- **What**: `parse_command` either returns a whole command or consumes nothing. The next attempt restarts at the beginning of that command.
- **Why**: no state machine to get wrong, and easy to test exhaustively (the test tries every possible split point).
- **Cost analysis (have this ready)**: re-parsing only re-reads *headers* (`*3`, `$5`), which are tiny. Bulk bodies are skipped by length, never scanned. The one real cost: a single 512 MB argument arriving in 16 KB chunks makes `parse_array` check "is the body complete yet?" ~32 000 times. Each check is O(1) per argument, so total cost is O(arguments × reads), not O(bytes²).
- **Change it when**: profiling shows commands with huge argument counts arriving slowly. Then save the parse position per connection, like Redis does.

### 4.4 `std::variant` keyspace
- **Why**: a key holds exactly one type. That makes `TYPE`, `WRONGTYPE` and "SET replaces a list" correct by construction, with one hash lookup per command.
- **Alternatives rejected**:
  - *One map per type* (the old design): a key could exist in several maps, and every command needed several lookups.
  - *Inheritance* (`struct Value { virtual ~Value(); }`, `StringValue : Value` ...): a heap allocation per value, virtual calls, and `dynamic_cast` for type checks.
- **Cost**: the variant is as large as its largest member. See [known issue #1](#known-issue-1-every-key-costs-about-5-kb), which is the most important thing in this whole guide.

### 4.5 `std::deque` for lists
- **Why**: O(1) push and pop at both ends, O(1) random access for `LINDEX`/`LRANGE`.
- **Alternatives**: `std::vector` (front insert is O(n): measured ~3500× slower), `std::list` (O(n) `LINDEX`, a heap allocation per element, poor cache use).
- **Redis does**: a *quicklist*, a linked list of compact byte arrays ("listpacks"), which uses much less memory per element.

### 4.6 Skip list with spans + hash map
- **Why**: `ZRANK` and `ZRANGE` by position need **rank** queries. `std::set` can't answer "what position is X?" faster than O(n). A skip list with spans answers it in O(log n), with far simpler code than an order-statistic tree (no rotations).
- **Alternatives**:
  - *Order-statistic tree* (red-black tree storing subtree sizes): same complexity, but much harder to write correctly.
  - *`__gnu_pbds::tree`*: works, but GCC-only and non-standard.
  - *Sorted vector*: O(1) rank by binary search, but O(n) insert.
- **Evidence**: the randomized model test plus ASan; the microbench.
- **Cost**: two structures to keep in sync; each member's string is stored twice (hash map key + node). Probabilistic balance: worst case O(n), but with p = 1/4 and 32 levels that's astronomically unlikely.

### 4.7 Expiry: lazy + sorted index
- **Why**: lazy expiry gives correctness (a command can never see an expired key); active expiry reclaims memory from keys nobody touches.
- **Why a sorted index rather than Redis's random sampling**: deterministic, and the work is exactly the number of expired keys. Redis samples 20 random keys with TTLs and repeats while more than 25% were expired. That needs no extra index memory, but expired keys can linger.
- **Cost**: about 60-80 bytes per TTL key for the `std::set` node, plus a second copy of the key string.
- **The 200-per-run cap**: bounds the pause. If a million keys expire at once, they're cleaned 2 000 per second while the server keeps answering. Lazy expiry hides any that remain.

### 4.8 AOF only (no snapshots), written before replying
- **Why**: one persistence mechanism, and replay reuses the parser and command code (no separate loader to get wrong).
- **Why before replying**: so `+OK` means "it's in the kernel". With `always`, it means "it's on disk".
- **Cost**: startup replays the whole log (rewrite keeps it short); the file is bigger than a binary snapshot.
- **The `dirty` flag**: only real changes are logged, so the log is a faithful history and replay is deterministic.

### 4.9 Command table of function pointers
- **Why**: O(1) dispatch; arity checked centrally; adding a command is one line.
- **Alternatives**: a giant `if/else` chain (O(n) string comparisons and one huge function), virtual classes per command (boilerplate), `std::function` (heavier, and captures aren't needed).

### 4.10 Home-grown test framework
- **Why**: zero dependencies, so `make test` works on any machine with g++. And it's 77 lines you can explain completely, which itself shows understanding of macros and static initialization.
- **Cost**: no fixtures, no filtering by name, no parallel runs. GoogleTest or Catch2 would be the professional choice for a bigger codebase.

---

## 5. Complexity of every operation

n = number of keys (or elements in the container), m = number of items returned, k = number of arguments.

| Command | Time | Notes |
|---|---|---|
| `GET`, `SET`, `APPEND`, `STRLEN`, `INCR`/`DECR` family | O(1) average | hash lookup. `APPEND` is O(len) for the copy |
| `MSET`, `MGET`, `DEL`, `EXISTS` | O(k) | k keys |
| `EXPIRE`, `PEXPIRE`, `PEXPIREAT`, `PERSIST` | O(log t) | t = keys with a TTL (`std::set` update) |
| `TTL`, `PTTL`, `TYPE` | O(1) | |
| `RENAME` | O(log t) | value never copied (`extract`) |
| `KEYS pattern` | **O(n × pattern)** | walks every key, blocks the server |
| `DBSIZE` | O(1) | counts expired-but-not-yet-removed keys too |
| `FLUSHALL` | O(n) | frees everything |
| `LPUSH`/`RPUSH` | O(k) | deque |
| `LPOP`/`RPOP`, `LLEN` | O(1) | |
| `LINDEX`, `LSET` | O(1) | deque random access |
| `LRANGE` | O(m) | |
| `LREM` | O(n) per pass, worst O(n × removed) | middle erase on deque shifts elements |
| `HSET`, `HGET`, `HDEL`, `HEXISTS`, `HINCRBY` | O(1) average per field | |
| `HGETALL`, `HKEYS`, `HVALS` | O(n) | |
| `ZADD` | O(log n) per member | |
| `ZSCORE`, `ZCARD` | O(1) | |
| `ZREM`, `ZRANK` | O(log n) | |
| `ZRANGE` | O(log n + m) | jump to rank, walk level 0 |
| `REWRITEAOF` | O(total data) | **synchronous** |
| Active expiry per tick | O(e × log t), e ≤ 200 | e = keys actually expired |
| Parsing a command | O(header bytes + k) | bulk bodies are not scanned |

---

## 6. Hard follow-up questions

These go past the standard ones in [Layer 4](4-presenting.md).

**"How much memory does one key use?"**
About 5 KB in the current code, which is a real problem. Then explain [known issue #1](#known-issue-1-every-key-costs-about-5-kb) and the fix. Turning a weakness into a measured fix is the best possible answer.

**"Prove INCR is atomic."**
There's no thread that could interleave: `execute_command` runs `cmd_incr` from start to finish before the loop looks at any other socket. The e2e test fires 500 INCRs from 20 concurrent processes and gets exactly 500. With a thread-per-client design and no lock, you'd see lost updates.

**"What if `write()` to the AOF fails (disk full)?"**
`Aof::flush` prints the error and keeps the buffer to retry on the next flush, but the reply is still sent. So the client is told `OK` for data that isn't durable. Real Redis stops accepting writes when AOF writes fail. That's [known issue #3](#known-issue-3-aof-write-errors-are-not-reported-to-clients).

**"What happens if the system clock jumps?"**
TTLs are absolute wall-clock times (`system_clock`), so a jump forward expires keys early and a jump back keeps them longer. That's the price of TTLs that survive restarts. Redis makes the same choice. A monotonic clock wouldn't mean anything across a restart.

**"Why does ZRANK need the hash map?"**
The skip list is ordered by `(score, member)`. To find a member you need its score to know where to look. The hash map gives the score in O(1); the skip list then finds the rank in O(log n). Without the map, finding the member alone would be O(n).

**"Your skip list uses `rand`-style randomness. Can an attacker make it degenerate?"**
The seed is fixed (`12345`), so the level sequence is predictable. But levels depend only on the *order of insertions*, not on the member names or scores an attacker chooses, so they can't force bad heights. Worst case, an attacker inserting many members still gets the expected O(log n) structure. (Hash tables are the more classic target for hash flooding; `std::hash` for strings isn't randomised in libstdc++.)

**"What if a client connects and sends nothing?"**
It costs one fd and an empty `Connection`. There's no idle timeout (Redis has `timeout`, off by default). Enough idle clients exhaust the fd limit (`ulimit -n`). After that, `accept` fails with `EMFILE` and the error is printed, but the listening socket stays readable, so the loop spins on it. A production server would reserve a spare fd to accept and close immediately, or pause accepting.

**"Why erase the input buffer once per read instead of per command?"**
`std::string::erase(0, k)` shifts the remaining bytes. Per command, a 1000-command pipeline would shift the buffer 1000 times: O(n²). Once per read, it's O(n).

**"How would you add MULTI/EXEC?"**
Queue commands per connection while in MULTI (a `std::vector<Args>` in `Connection`), and on EXEC run them back-to-back inside one `process_input` call. Since the loop is single-threaded, nothing can interleave, so atomicity is free. WATCH would need per-key version numbers.

**"How would you add replication?"**
The AOF is already a stream of data-changing commands in RESP. A replica connects and receives a snapshot (a rewrite's output), then the live stream of the same bytes appended to the AOF buffer. Replicas replay them through `execute_command`.

**"Why not use `std::map` for the keyspace? You'd get sorted KEYS."**
`std::map` lookup is O(log n) with a pointer chase per level, while a hash map is O(1) with one or two cache misses. Lookups are the hot path; sorted iteration is rare. Redis uses a hash table too.

**"Why `std::set<pair<time, key>>` and not a priority queue?"**
`std::priority_queue` can't remove an arbitrary element. When a key is deleted or its TTL changes, the old entry must be removed from the index. `std::set::erase({time, key})` does that in O(log n).

---

## 7. Known issues and limitations

Found by reading every line and measuring. Severity is for a production setting; for a learning project most are reasonable trade-offs. **Mentioning them yourself, with a fix, is a strength.**

### Known issue #1: every key costs about 5 KB
- **Severity**: high (memory).
- **What**: `Value` is `std::variant<std::string, List, Hash, SortedSet>`. A variant always reserves room for its *largest* alternative. `SortedSet` contains a `SkipList`, which contains a `std::mt19937` random generator with ~5 KB of internal state. So `sizeof(Value)` is 5088 bytes, and **every key, even a 1-byte string, uses ~5 KB**.
- **Evidence** (measured):
  ```
  sizeof(Value) = 5088, sizeof(Entry) = 5096        (std::string is 32, deque 80)
  100 000 x SET kN v  ->  RSS ~505 MB   (real Redis: roughly 10 MB)
  ```
  Reproduce: `printf` 100k RESP `SET`s into a file, pipe it with `redis-cli --pipe`, then `grep VmRSS /proc/<pid>/status`.
- **Fix (verified)**: make the generator shared instead of per-list. In `skiplist.h` remove the `rng_` member; in `skiplist.cpp` put `static std::mt19937 rng_{12345};` at the top of `random_level()`. Results, measured on a scratch copy:
  ```
  sizeof(Value) = 88, sizeof(Entry) = 96
  100 000 x SET kN v  ->  RSS ~21 MB      (about 25x less), 31/31 tests still pass
  ```
  The trade-off: all sorted sets now share one random sequence. That doesn't matter for correctness, because levels only need to be random, not independent per list. (A function-local `static` isn't thread-safe to *use* concurrently, but the server is single-threaded.)
- **Alternative fix**: store the big, rare alternative behind a pointer, `std::variant<std::string, List, Hash, std::unique_ptr<SortedSet>>`. That also makes `Entry` movable.
- **Interview line**: *"I measured memory per key and found 5 KB. The cause was a 5 KB RNG inside the variant's largest alternative. Making it shared dropped memory 25×."* This shows you understand `sizeof`, variants, and measurement-driven engineering.

### Known issue #2: output buffers are unbounded
- **Severity**: medium (memory exhaustion by one client).
- **What**: a client that pipelines `GET bigkey` forever but never reads makes `conn.output` grow without limit (`server.cpp:206-234`). The *input* side is capped at 1 GB; the output side isn't.
- **Fix**: after `process_input`, if `conn.output.size() - conn.output_sent` exceeds a limit (Redis uses 0 = unlimited for normal clients by default, but enforces limits for replicas and pub/sub), close the connection. Also stop reading from that client while its output is large (backpressure).

### Known issue #3: AOF write errors are not reported to clients
- **Severity**: medium (durability).
- **What**: if `write()` fails (disk full, I/O error), `Aof::flush` logs it and keeps the buffer, but the replies are still sent (`server.cpp:177-179`). Clients see `OK` for data that may be lost on restart. `fdatasync` errors are ignored too.
- **Fix**: make `flush()` return `bool`. On failure, set a "write error" state and reply `-MISCONF` to write commands until a flush succeeds. That's what Redis does.

### Known issue #4: `SET ... EX` is logged as two records
- **Severity**: low (rare crash window).
- **What**: `SET k v EX 10` is logged as `SET k v` followed by `PEXPIREAT k <ms>`. Both are in the same buffer and usually in one `write()`. But if the machine crashes after the first record reached disk and before the second did, replay restores `k` **without a TTL**, so it lives forever.
- **Fix**: log one command, `SET k v PXAT <ms>` (Redis 7 propagates it this way), and add `PXAT` support to `cmd_set`.

### Known issue #5: blocking work on the event loop
- **Severity**: medium at scale.
- **What**: `REWRITEAOF` builds the whole new file in memory and writes it synchronously. `KEYS *` walks every key. `load_aof` reads the whole file into one string (so startup memory briefly doubles). `FLUSHALL` frees everything in one go.
- **Fix**: rewrite in a `fork()`ed child (copy-on-write snapshot) while the parent buffers new writes and appends them when the child finishes. Replace `KEYS` with an incremental `SCAN`. Stream the AOF during load.

### Known issue #6: `DBSIZE` counts logically expired keys
- **Severity**: low (matches Redis).
- **What**: `DBSIZE` returns `data_.size()`, which includes keys whose time has passed but that active expiry hasn't removed yet (at most ~100 ms behind, or more if many keys expire at once). `KEYS` filters them out. Real Redis behaves the same way.

### Known issue #7: AOF lost silently if reopening fails after a rewrite
- **Severity**: low (very unlikely).
- **What**: after `rename()`, `Aof::rewrite` closes and reopens the file. If `open()` fails, `fd_` stays `-1`, and every later `flush()` returns early, so the buffer grows forever and nothing is persisted. `REWRITEAOF` does reply with an error, but later writes don't.
- **Fix**: open the new file *before* closing the old fd, or treat the failure as fatal for writes (see #3).

### Known issue #8: protocol and feature gaps
These are features, not bugs; just know them so you never over-claim.
- Inline commands don't support quotes (`SET k "a b"` typed in telnet gives 4 arguments). Redis supports quotes there.
- `KEYS` glob supports `*` and `?` but not `[abc]` or `\` escapes.
- Missing options: `SET ... GET/KEEPTTL/EXAT/PXAT`, `ZADD NX/XX/GT/LT/INCR/CH`, `ZRANGE ... REV/BYSCORE/LIMIT`, `ZREVRANGE`, `ZRANGEBYSCORE`, `ZINCRBY`.
- Missing types and features: sets (`SADD`...), pub/sub, transactions, `SELECT` (multiple databases), `AUTH`, eviction (`maxmemory`), `INFO`, `SCAN`, RDB snapshots, replication.
- `COMMAND` replies with an empty list (enough for `redis-cli`'s startup check). `CONFIG GET` only knows `save` and `appendonly`.
- No idle-client timeout; no protection against running out of fds (see §6).

### Known issue #9: client limitations
- `-p abc` becomes port 0 (no validation of `atoi`).
- No automatic reconnect: if the server restarts, the REPL exits with "connection to server lost".
- No line editing or history (redis-cli uses `linenoise`).
- `ReplyReader` recursion depth isn't limited, so a malicious server sending deeply nested arrays could overflow the stack.

### Known issue #10: small documentation mismatches
- The README says "34 end-to-end checks"; with `redis-cli` installed there are 35 (the compatibility check is skipped otherwise). Both are correct in context.
- README micro benchmark numbers are from one specific machine. Re-measure and quote your own.

### Known issue #11: `make benchmark` can hang forever
- **Severity**: low (tooling), but it bites exactly when you try to compare against Redis.
- **What**: `bench/run_benchmarks.sh` waits for a server with `redis-benchmark -p PORT -n 1 -t ping_mbulk -q` in a loop, expecting it to *fail* while the server isn't up yet. But `redis-benchmark` 7.0.15 doesn't fail on a refused connection: it **spins at 100% CPU forever** (verified: `timeout 5 redis-benchmark -p 7999 -n 1 -t ping_mbulk -q` gets killed by the timeout). mini-redis usually starts before the first probe, so its half works. `redis-server` is slower to start, so the comparison hangs.
- **Fix (verified, the full comparison then completes)**: probe with `redis-cli` instead, which fails fast:
  ```bash
  if redis-cli -p "$1" PING > /dev/null 2>&1; then return 0; fi
  ```

---

## 8. Phrases to avoid, and what to say instead

| Avoid | Why it hurts | Say instead |
|---|---|---|
| "It's as fast as Redis." | Easy to disprove, and you can't back it up without numbers. | "On my machine it reaches X% of Redis's throughput with the same `redis-benchmark` settings." (measure first) |
| "It's production ready." | See section 7. | "It implements the core of Redis's architecture. Here's what I'd need to add for production." |
| "Skip lists are O(log n)." | Incomplete. | "Expected O(log n); worst case O(n), but with random levels that's vanishingly unlikely." |
| "Single-threaded is always better." | It's a trade-off. | "Single-threaded is right when work per command is tiny and you want atomicity without locks. It's wrong for CPU-heavy commands." |
| "fsync makes it durable." | Only with `always`. | "Durability depends on the policy: `always` loses almost nothing on power loss, `everysec` up to a second." |
| "I used `volatile` for thread safety." | Wrong: `volatile` is not a threading tool. | "`volatile sig_atomic_t` is the one type the standard guarantees is safe to write from a signal handler." |
| "I wrote it all myself from scratch" (if not true) | Dishonesty is fatal if discovered. | Use the honest framing from [Layer 4](4-presenting.md#honesty-note). |

---

Next: [Layer 8: Reference (commands, limits, errors, glossary)](8-reference.md)
