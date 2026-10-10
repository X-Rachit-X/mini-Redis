# Interview preparation: mini-redis

These are your **master notes** for this project. Everything else in `docs/` is reference material; this is the file you revise from.

It follows one simple idea: **you should be able to explain the project without a script, answer follow-up questions because you understand the code, and be honest about trade-offs and limits.**

The same five-part structure works for your other projects (Tessera, CortexOne). A blank template is at the end.

---

## Contents

- [Part 0. How to use these notes (start here)](#part-0-how-to-use-these-notes)
- [Part 1. Project overview](#part-1-project-overview)
- [Part 2. Architecture and request flow](#part-2-architecture-and-request-flow)
- [Part 3. Core features and how they're built](#part-3-core-features)
- [Part 4. Failures, edge cases and trade-offs](#part-4-failures-edge-cases-and-trade-offs)
- [Part 5. Interview answers: speeches for every time limit, and 25 questions](#part-5-interview-answers)
- [Part 6. Study plan from zero](#part-6-study-plan-from-zero)
- [Part 7. Mastery tracker](#part-7-mastery-tracker)
- [Appendix. Blank template for your other projects](#appendix-blank-template)

---

# Part 0. How to use these notes

### The goal is understanding, not memorising
Interviewers don't stop at your pitch. They pick one thing you said and ask "why?" two or three times. Memorised answers break on the second "why". Understanding doesn't.

So, for every feature, these notes give you:
- **What** it does, in one or two plain sentences.
- **Why** it's needed: what goes wrong without it.
- **How** it's built: the actual files and functions in this repository.
- **Example**: something you can run and see.
- **Trade-off**: what was given up, and what the alternative was.
- **Likely follow-up**: the question that usually comes next, with a short answer.

### Your daily workflow (one feature per session, 45–60 minutes)

1. **Read the code first.** Open the files listed for that feature. Don't start with a generic explanation of the technology.
2. **Understand it.** Use the walkthrough in `docs/3-walkthrough/`, or ask AI: "what does this function do, why does it exist, what calls it?"
3. **Write it in your own words.** Add a few lines under the feature in this file, in the "My notes" space. If you can't write it simply, you don't understand it yet.
4. **Verify it.** Run it, read its test, or break it on purpose and watch a test fail. (`docs/0-learning-path.md` has "break it" experiments.)
5. **Test yourself.** Close everything and explain the feature out loud, as if to a classmate. Write down every question you couldn't answer, and study those next time.

### Only claim what is verified
If you only *think* something works a certain way, don't say it as fact. Say "I believe" and, better, go and check. The table below separates what has actually been checked from what hasn't.

| Claim | Status | How it was checked |
|---|---|---|
| 53 commands, 4 data types | ✅ verified | command table in code, unit + end-to-end tests |
| 32 unit tests, 35 end-to-end checks pass, also under AddressSanitizer/UBSan | ✅ verified | `make test`, `make SAN=1 test` (34 e2e when the official `redis-cli` isn't installed) |
| Works with the official `redis-cli` and `redis-benchmark` | ✅ verified | e2e compatibility check; benchmark runs |
| Throughput vs Redis: 74–137% (one hot key), ~67–87% (630 000 keys) | ✅ verified on one 4-core VM | `make benchmark`. **Re-measure on your own machine before quoting.** |
| Memory: 175 B per key (was ~5 KB), Redis 93 B | ✅ verified | memory measurement + `entries_stay_small` test |
| Micro benchmarks (deque >1000×, ZRANK >3000×, parser ~17 M cmds/s) | ✅ verified | `make microbench` (numbers vary by machine) |
| Survives restart, repairs a half-written AOF tail | ✅ verified | e2e restart test, `aof_truncated_tail_is_repaired` |
| Deployment (systemd, ttyd, Caddy, landing page) | ✅ verified on a fresh Ubuntu 24.04 VM | full run of `docs/9-deploy.md`, incl. reboot and memory cap |
| Real Let's Encrypt certificate, your cloud provider's firewall | ⚠️ not yet verified | the test used Caddy's local certificate authority. Check on your VM. |
| Docker image | ✅ verified | build, run, health check, graceful `docker stop` |
| Performance under heavy real-world load, many machines | ❌ not tested | don't claim it |

### Status labels
When you study a feature, give it a status in the tracker (Part 7):

| Status | Meaning |
|---|---|
| ⬜ Not started | haven't read the code yet |
| 🟨 Read | read the code, but can't explain it without notes |
| 🟧 Can explain | can explain what and why out loud, without notes |
| 🟦 Can trace | can follow it through the actual code, function by function |
| 🟩 Can defend | can answer failure and trade-off questions about it |
| ⭐ Can change | have modified, tested or debugged it yourself |

Aim for 🟩 on the features on your resume (event loop, parser, skip list, AOF, the memory bug) and 🟧 on the rest.

---

# Part 1. Project overview

### What is it?
mini-redis is **my own version of Redis**, written from scratch in C++.

Redis is a very popular database that keeps all its data **in memory (RAM)** instead of on disk, so it answers in microseconds. Companies use it for caching, login sessions, counters, queues and leaderboards.

mini-redis does the core of what Redis does:
- It's a **server** that stores data and a **client** (command-line tool) to talk to it.
- It speaks the **same network protocol as real Redis** (called RESP). So the official Redis tools (`redis-cli`, `redis-benchmark`) work with my server without knowing it isn't Redis.
- It supports **4 data types** (strings, lists, hashes, sorted sets) and **53 commands**.
- Keys can **expire** after a time (TTL).
- Data **survives restarts and crashes**, because every change is written to a log file on disk.

### What problem does it solve?
For me, it's a learning project: it shows how a real database server works inside. The problems it has to solve are the same ones real servers face:
- **Many clients at once**, without data getting mixed up.
- **Network data arriving in pieces.** One message can arrive split in two, or many messages can arrive together.
- **Fast operations** on sorted data, like "what rank is this player?"
- **Not losing data** when the program or the machine crashes.

### Who would use something like it?
Any backend application that needs very fast shared data: a website storing user sessions, a game storing a leaderboard, an API counting requests per user, a job system using a queue.

### Why is it interesting?
Because it touches almost everything in systems programming in one small codebase: network sockets, an event loop, protocol parsing, data structures, durability on disk, memory usage, testing, benchmarking and deployment.

### What makes it more than a basic implementation?

| A basic version... | mini-redis... |
|---|---|
| uses one thread per client and a big lock | uses **one thread with an event loop (epoll)**, like real Redis: no locks needed |
| assumes each network read is exactly one command | **buffers and parses incrementally**, so split and pipelined commands work |
| stores lists in a vector, sorted sets in a map | uses a **deque** for lists and a **skip list with spans** for sorted sets (fast rank queries) |
| saves a text dump now and then | keeps an **append-only log**, repairs a half-written last entry, and compacts the log **atomically** |
| is "tested" by trying a few commands | has **unit tests, a randomized test, end-to-end tests, sanitizers and CI** |
| claims to be fast | is **benchmarked against real Redis**, including where it's slower |

**My notes:**
> _(Write the overview in your own words here. Five sentences maximum.)_

---

# Part 2. Architecture and request flow

### The picture

```
                 ┌──────────────────── mini-redis-server (one thread) ────────────────────┐
 client ─TCP──▶  │ epoll loop → input buffer → RESP parser → command table → Database      │
                 │     ▲                                              │          │         │
 client ◀─TCP──  │     └──── output buffer ◀── RESP writer ◀──────────┘          ▼         │
                 │                                                   AOF log ──▶ disk     │
                 │ every 100 ms: delete expired keys     every 1 s: fsync the log         │
                 └─────────────────────────────────────────────────────────────────────────┘
```

### What each part does, and why it exists

| Part | What it does (simple words) | Why it exists | Files |
|---|---|---|---|
| **Event loop** | One thread waits until any client has data, then handles it | Serve many clients without threads or locks | `server/server.cpp` |
| **Connection** | Holds each client's unread input and unsent output | Network data arrives in random pieces | `server/connection.h` |
| **RESP parser** | Turns bytes into a command like `["SET","name","Alice"]` | Find where one command ends and the next begins | `server/resp_parser.cpp` |
| **Command table** | Maps a name like `GET` to the function that runs it | Fast lookup, argument checks in one place | `server/commands.cpp` |
| **Command handlers** | The logic of each command | One file per data type keeps it readable | `server/cmd_*.cpp` |
| **Database** | All keys and values, plus expiry times | The actual data | `server/database.cpp` |
| **Skip list / sorted set** | Keeps members ordered by score | Fast rank and range queries | `server/skiplist.cpp`, `sorted_set.cpp` |
| **AOF** | Writes every change to a log file; replays it at startup | Survive restarts and crashes | `server/aof.cpp` |
| **RESP writer** | Turns replies into bytes like `+OK\r\n` | The client must understand the answer | `server/resp_writer.cpp` |
| **Client** | Reads what you type, sends it, prints the reply | A tool to use the server | `client/*` |

### One request, start to finish (`SET name Alice`)

1. **Client:** splits the line into `["SET","name","Alice"]` (`split_args`), encodes it as RESP (`encode_command`), and sends it (`send_all`).
2. **Server:** `epoll_wait` wakes up because that client's socket has data.
3. `on_readable` reads up to 16 KB into the client's **input buffer**.
4. `process_input` loops: `parse_command` → **Ok** gives one full command. (**Incomplete** means wait for more bytes; **Error** means reply with an error and close the connection.)
5. `execute_command` finds `SET` in the command table, checks the number of arguments, and calls `cmd_set`.
6. `cmd_set` stores the value in the database and marks the command as **dirty** (it changed data).
7. Because it's dirty, the command is added to the **AOF buffer**.
8. After all complete commands in the buffer are processed, the AOF buffer is **written to the file**, *before* any reply is sent.
9. The reply `+OK\r\n` goes into the client's **output buffer**, and `on_writable` sends it. If the socket is full, the server asks epoll to say when it can write again.
10. **Client:** `ReplyReader::read_reply` reads the reply, and `format_reply` prints `OK`.

**Key sentence to remember:** *"One thread, buffers in both directions, the parser decides where commands end, the log is written before the reply."*

**My notes:**
> _(Draw this diagram from memory on paper. Then compare.)_

---

# Part 3. Core features

Eight features. The first five are the ones to know deeply.

---

## Feature 1. The event loop (one thread, epoll)

**What:** One thread serves all clients. It sleeps in `epoll_wait` until some socket is ready, handles it quickly, and goes back to waiting.

**Why:** Each command takes microseconds, because everything is in RAM. The slow part is *waiting for the network*. One thread can wait on thousands of sockets at once with epoll. With one thread there are **no locks**, no data races, and every command runs completely before the next one starts (it's **atomic**).

**How (in the code):** `server/server.cpp`
- `Server::run`: loads the AOF, creates the listening socket, creates epoll.
- `Server::event_loop`: the `while` loop around `epoll_wait`.
- `accept_new_clients`: accepts, makes the socket non-blocking, turns off Nagle's delay.
- `on_readable` / `on_writable`: read input, send output.
- `watch_for_writes`: turns EPOLLOUT on only while there's unsent output.
- `run_background_jobs`: expiry every 100 ms, fsync every second.

```
while not stopped:
    ready = epoll_wait(timeout = 100 ms)
    for each ready socket:
        listening socket → accept new clients
        readable         → read 16 KB, run complete commands, send replies
        writable         → send the rest of the output
    background jobs (expiry, fsync)
```

**Important details:**
- **Non-blocking sockets:** `read()` never waits. With no data, it returns "try again" (EAGAIN).
- **Level-triggered epoll, one read per event:** if more data is waiting, epoll tells us again next time. That's fair: one busy client can't starve the others.
- **EPOLLOUT only when needed:** a socket is almost always writable, so leaving it on would wake the loop constantly and burn 100% CPU.

**Example:** the e2e test runs 20 clients × 25 `INCR` at the same time, and the final count is exactly 500. No update is lost, because commands never overlap.

**Trade-off:** uses one CPU core. A slow command (like `KEYS` on a huge database) blocks everyone. The alternative, one thread per client (my first version), needs a lock around the database, and that lock makes commands run one at a time anyway.

**Likely follow-up:** *"Wouldn't more threads be faster?"* → "Not for the command work, which is tiny. Real Redis also runs commands on one thread. To use more cores you'd move only socket reading/writing to other threads (Redis 6 does this) or run several instances."

**My notes:**
> _ _

---

## Feature 2. The RESP parser and pipelining

**What:** turns raw bytes into commands, and handles any number of commands per read, including half of one.

**Why:** TCP is a **byte stream, not a message stream**. If a client sends two commands, the server might receive both in one read, or one and a half, or one split into three pieces. My first version treated each read as one command, which broke under load.

**How:** `server/resp_parser.cpp`, function `parse_command`. A command looks like this:
```
*3\r\n  $3\r\nSET\r\n  $4\r\nname\r\n  $5\r\nAlice\r\n
 ↑ 3 parts  ↑ 3 bytes      ↑ 4 bytes       ↑ 5 bytes
```
It returns one of three answers:
- **Ok** + how many bytes the command used,
- **Incomplete** (the bytes so far are fine, but more are needed),
- **Error** (the bytes are invalid).

**Important details:**
- Values are **length-prefixed** (`$5`), so the parser never searches inside a value. It *calculates* where the value ends. That makes it **binary safe**: a value can even contain `\r\n`.
- `process_input` (in `server.cpp`) loops: parse → run → parse → run, until only an incomplete command (or nothing) is left. That's **pipelining**: 1000 commands in one read give 1000 replies, sent together.
- **Safety limits:** at most 1M arguments, 512 MB per argument, 64 KB per header line, 1 GB per client buffer. Beyond that, the client gets an error. Same limits as Redis.
- It also accepts "inline" commands (plain text like `PING`), so you can type into `telnet`.

**Example:** the unit test `every_partial_prefix_is_incomplete` cuts a command at every possible byte and checks each piece says Incomplete. The e2e test sends a command in two parts with a pause between, and sends 1000 PINGs in one write.

**Trade-off:** when more data arrives, it re-parses the incomplete command from its start. That's simpler than saving parser state, and cheap, because only the small headers are re-read, never the values.

**Likely follow-up:** *"What if someone sends a 10 GB command?"* → "There are limits. Past 512 MB per argument or 1 GB per buffer, the client gets an error and is disconnected."

**My notes:**
> _ _

---

## Feature 3. One typed value per key (`std::variant`) and the command table

**What:** every key holds exactly one value: a string, a list, a hash or a sorted set. Using the wrong command on a key gives Redis's `WRONGTYPE` error.

**Why:** my first version had three separate maps, so one key could be a string *and* a list at the same time.

**How:**
- `server/database.h`: `using Value = std::variant<std::string, List, Hash, SortedSet>;` and `struct Entry { Value value; int64_t expire_at; };`. The keyspace is `unordered_map<string, Entry>`.
- `server/command_helpers.h`: `find_typed<T>` returns the value if the key holds a T, `nullptr` if the key is missing, and sets `wrong_type` if the key holds something else. `find_or_create<T>` also creates it when missing.
- `server/commands.cpp`: a table from name to `{handler function, arity}`. `execute_command` upper-cases the name, looks it up, checks the argument count, runs the handler, and logs to the AOF if the handler set `dirty`.

```cpp
List* list = find_or_create<List>(db, key, wrong_type);
if (wrong_type) → reply WRONGTYPE
list->push_front(value);          // LPUSH
ctx.dirty = true;                 // changed data → goes to the AOF
```

**Important details:**
- A `std::variant` is **as big as its largest type**. This caused the 5 KB bug (Feature 8).
- Empty lists, hashes and sorted sets are **deleted**, like in Redis.
- Lists use `std::deque`: adding or removing at the front is O(1). A vector would have to shift everything (over 1000× slower in my micro benchmark).

**Example:** `RPUSH jobs a` then `GET jobs` → `(error) WRONGTYPE ...`

**Trade-off:** every access checks the type, but that's one cheap check, and it makes wrong states impossible.

**Likely follow-up:** *"How do you add a new command?"* → "Write one handler function in the right `cmd_*.cpp` file and add one line to its register function, with the arity."

**My notes:**
> _ _

---

## Feature 4. Sorted sets with a skip list

**What:** `ZADD`, `ZRANGE`, `ZRANK` and friends: members ordered by a score, like a leaderboard.

**Why:** we need fast insert, delete, *and* "what position is this member?" `std::map` can insert in O(log n), but finding a position means walking element by element: O(n).

**How:** `server/skiplist.cpp` and `server/sorted_set.cpp`.
- A **skip list** is a sorted linked list with "express lanes" on top. Each node gets a random height, so a search jumps over most nodes: O(log n) on average.
- Each link also stores its **span**, meaning how many nodes it jumps over. Adding the spans along the search path gives the **rank** in O(log n). This is exactly what real Redis does.
- `SortedSet` = a **hash map** (member → score, for O(1) `ZSCORE`) + the **skip list** (for order). Both are always updated together.

```
level 1:  head --(2)--> b --(2)--> d
level 0:  head → a → b → c → d
rank of d = 2 + 2 = 4th → index 3
```

**Important details:**
- Ties on equal scores are ordered alphabetically by member, like Redis.
- `random_level`: each extra level has a 1-in-4 chance, so a node has 1.33 levels on average.
- The random number generator is **shared** (a static inside `random_level`). That's the fix for the memory bug (Feature 8).

**Example:** `ZADD board 300 carol 100 alice 200 bob` → `ZRANGE board 0 -1` gives alice, bob, carol. `ZRANK board carol` → 2.

**How I know it's correct:** the test `sorted_set_matches_reference_model` does 20 000 random adds, updates and removes (with many equal scores) and compares everything with `std::set` every 500 steps. It also runs under AddressSanitizer.

**Trade-off:** members are stored twice (hash map + list). The worst case is O(n) if the random heights are very unlucky, but that's practically impossible.

**Likely follow-up:** *"Why not a balanced tree?"* → "Same speed on average, simpler code (no rotations), and spans give rank cheaply. A tree would need extra counts in every node for that."

**My notes:**
> _ _

---

## Feature 5. Persistence: the append-only file (AOF)

**What:** every command that changes data is appended to a file. At startup, the file is replayed to rebuild the data.

**Why:** RAM is lost when the program stops. Without this, a restart means an empty database.

**How:** `server/aof.cpp`
- `log_command`: adds the command to a buffer (in RESP, the same format clients send).
- `flush`: writes the buffer to the file. This happens **before replies are sent**.
- `fsync_now`: forces the data onto the physical disk. Policy: `always`, `everysec` (default) or `no`.
- `load_aof`: at startup, reads the file and runs each command through the normal `execute_command`.
- `rewrite` (the `REWRITEAOF` command): writes a compact version of the file.

**Important details (great interview material):**
- **Absolute times.** `EXPIRE key 100` means "100 seconds from *now*". Replaying it tomorrow would be wrong, so the log stores `PEXPIREAT key <exact time in ms>` instead.
- **Crash in the middle of a write.** The file may end with half a command. At startup the parser says Incomplete at the end, so the file is **cut back** to the last complete command. Garbage in the *middle* is real corruption, so the server refuses to start rather than silently lose data.
- **Atomic rewrite.** Write a temp file → fsync it → `rename()` it over the old file. `rename` is atomic: after a crash you have either the old file or the new one, never half of each. Then the server reopens the file, because its old file handle still points at the replaced file.
- **write vs fsync.** `write` puts data in the operating system's memory. A *program* crash loses nothing that was written. Only a *power cut* can lose up to ~1 second in `everysec` mode.

**Example:** e2e test: write data, stop the server with SIGTERM, start it again, and all four types plus a TTL are back.

**Trade-off:** startup time grows with the log until you compact it. `REWRITEAOF` **blocks** the server while it runs (real Redis uses `fork()` to do it in the background).

**Likely follow-up:** *"What do you guarantee if the machine loses power?"* → "With `everysec`, at most about one second of writes is lost. With `always`, essentially nothing, but it's slower."

**My notes:**
> _ _

---

## Feature 6. Key expiry (TTL)

**What:** `SET otp 4821 EX 10` → the key disappears after 10 seconds.

**How:** `server/database.cpp`
- **Lazy:** every lookup goes through `Database::find`, which deletes the key if its time has passed. No command can ever see an expired key.
- **Active:** 10 times a second, `remove_expired` deletes keys nobody touched. A `std::set` of (expiry time, key) keeps the soonest one first, so it only looks at keys that really expired, at most 200 per round.

**Trade-off:** the sorted index costs extra memory per key with a TTL. Real Redis instead checks 20 random keys at a time: less memory, but expired keys can stay around a bit longer.

**Likely follow-up:** *"Why wall-clock time?"* → "Expiry times are saved in the log and must mean the same moment after a restart. The downside: if the system clock jumps, TTLs jump too. Redis has the same issue."

**My notes:**
> _ _

---

## Feature 7. Testing, benchmarking and CI

**What exists:**
- **32 unit tests** (`tests/`, with a tiny framework of my own in `test_framework.h`): the parser, database, skip list, commands, AOF.
- **A randomized model test** for the skip list (Feature 4).
- **35 end-to-end checks** (`tests/e2e_test.sh`): the real server and real client over real TCP. Pipelining, split packets, protocol errors, 20 concurrent clients, restart recovery, compatibility with the official `redis-cli`.
- **Sanitizers:** `make SAN=1 test` runs everything with AddressSanitizer (memory errors) and UBSan (undefined behaviour).
- **CI:** GitHub Actions runs both builds on every push.
- **Benchmarks:** `make microbench` (data structure choices) and `make benchmark` (`redis-benchmark` against mini-redis and real Redis).

**Why it matters in an interview:** "how do you know it works?" is one of the most common questions. A randomized test against a simple model is a strong, specific answer.

**My notes:**
> _ _

---

## Feature 8. The memory bug: every key cost 5 KB (your best story)

**What happened:** I loaded 100 000 tiny keys and measured the server's memory. It was about **5 KB per key**, for one-byte values. Real Redis uses 93 bytes.

**Why:** a `std::variant` is always as big as its **largest** type. The largest was the sorted set, which held a skip list, which held its own random number generator (`std::mt19937`, about 5 KB of internal state). So **every** key, even a short string, reserved 5 KB.

**Fix:** the generator became a single **shared** static inside `random_level`. `sizeof(Value)` dropped from 5088 to 88 bytes, and memory per key to **175 bytes** (~30× less, within 2× of Redis).

**Guard:** the unit test `entries_stay_small` fails if an entry ever grows past 256 bytes again.

**Why it's a great story:** it shows *measuring instead of assuming*, a real C++ insight (variant size), a clean fix, and a test to prevent it coming back.

**Likely follow-ups:**
- *"Where do the 175 bytes go?"* → "96 for the entry, then the key string, the hash-table node and the allocator's overhead. Redis uses compact encodings and a better allocator."
- *"Is a shared generator safe with threads?"* → "Not in general, but the server is single-threaded. With threads I'd make it `thread_local`."

**My notes:**
> _ _

---

## Feature 9 (brief). Deployment

What exists, and you should be able to describe it in 30 seconds:
- **systemd service:** starts at boot, restarts after crashes, runs as a user with no login, memory capped at 256 MB, sandboxed.
- **Public demo:** Caddy (HTTPS + password) → ttyd (a terminal in the browser) → my CLI → my server. Only ports 22, 80 and 443 are open. A static landing page with commands to try.
- **Docker image:** runs as a non-root user, with a health check.
- **The server listens on 127.0.0.1 by default**, because it has no password.

Details: `docs/9-deploy.md` (steps), `docs/10-deployment-concepts.md` (concepts), `docs/11-caddy.md` (Caddy).

---

# Part 4. Failures, edge cases and trade-offs

### What can fail, and what happens

| What fails | What the server does | Where |
|---|---|---|
| A command arrives in pieces | waits for the rest (Incomplete) | `parse_command` |
| A client sends garbage | replies `-ERR Protocol error`, then closes that connection | `process_input` |
| A client sends a huge command | error and disconnect past the limits | parser limits, `MAX_INPUT_BUFFER` |
| A client disconnects mid-reply | that connection is closed; the server is fine (SIGPIPE is ignored) | `on_writable`, `install_signal_handlers` |
| A client never reads its replies | its output buffer keeps growing (**known gap**) | `Connection::output` |
| Ctrl+C / `systemctl stop` / `docker stop` | flag set → loop exits → log flushed and fsynced | `on_stop_signal`, `shutdown` |
| The process crashes | data written before the crash is in the OS cache → replayed at start | AOF |
| Power cut | up to ~1 s lost with `everysec` | `fsync_now` |
| Half-written last log entry | cut off at startup, continue | `load_aof` |
| Corrupt log in the middle | refuse to start, print the byte position | `load_aof` |
| Wrong type for a command | `WRONGTYPE` error, nothing changes | `find_typed` |
| Bad number, overflow on `INCR` | error before anything changes | `parse_integer`, overflow check |
| Memory grows too much | no limit inside the server (**known gap**); on the VM, systemd's `MemoryMax` stops and restarts it | `deploy/mini-redis.service` |

### What the system actually guarantees
- **Atomic commands:** one thread, so a command is never interrupted by another.
- **A reply means it's logged:** the log is written before any reply is sent.
- **Durability:** depends on the fsync policy (see Feature 5). It's honest to say "up to one second on power loss by default".
- **Exact replay:** relative TTLs are stored as absolute times.

### Design choices and the alternatives

| Choice | Alternative | Why this one |
|---|---|---|
| One thread + epoll | thread per client + lock | no locks, atomic commands, scales to many connections |
| Re-parse incomplete commands | save parser state between reads | much simpler; only small headers are re-read |
| `std::variant` per key | separate map per type | a key can only have one type |
| `std::deque` for lists | `std::vector` | O(1) at both ends |
| Skip list with spans | `std::map` / balanced tree | rank and range in O(log n), simpler than a tree with counts |
| Sorted expiry index | random sampling (Redis) | exact and simple; costs memory |
| Append-only log | periodic snapshot | loses less on a crash; reuses the parser for replay |
| Synchronous rewrite | `fork()` in the background (Redis) | simple; but blocks the server while it runs |
| Bind to 127.0.0.1 by default | listen on everything | no password, so only local clients by default |

### Known limitations (say these openly)
No password or encryption. No `maxmemory` or eviction. No cap on a slow client's output buffer. The AOF rewrite blocks. Single machine: no replication or failover. Missing Redis features: Pub/Sub, transactions (`MULTI/EXEC`), sets, `SCAN`.

**My notes:**
> _ _

---

# Part 5. Interview answers

## Speeches for every time limit

Don't memorise these word for word. Learn the **order of ideas**, and say it your own way. Practise each one with a timer.

### ⏱ 15 seconds: "What's this project?" (one breath)
> "mini-redis is a Redis-compatible database server I built from scratch in C++. It runs on one thread with an event loop, supports strings, lists, hashes and sorted sets, saves to disk with a log, and the official Redis tools work with it unchanged."

### ⏱ 45 seconds: the pitch (when they say "tell me about a project")
> "One project I'm proud of is mini-redis, a Redis-compatible in-memory database written from scratch in C++.
>
> It runs on a single thread with an epoll event loop, the same model Redis uses, so there are no locks and every command is atomic. It parses the Redis protocol incrementally, so it handles commands split across packets and pipelined commands, and the official redis-cli and redis-benchmark work against it.
>
> It has four data types, including sorted sets on a skip list that answers rank queries in log time, keys that expire, and an append-only log that survives crashes.
>
> The part I learned the most from was measuring it against real Redis. At one point every key cost five kilobytes, and tracking that down taught me a lot about C++."

*(Stop here. That last sentence is bait: let them ask.)*

### ⏱ 2 minutes: the explanation (when they say "walk me through it")
Order of ideas, about 20 seconds each:
1. **What it is** (the 15-second version).
2. **How a request flows:** one thread waits in epoll → reads into the client's buffer → the parser finds complete commands → the command table runs them → the log is written → replies are sent.
3. **Why one thread:** commands take microseconds; the waiting is the slow part, and epoll handles that for all clients at once. No locks, atomic commands.
4. **One interesting data structure:** the skip list with spans for leaderboard-style rank queries.
5. **Durability:** absolute times in the log, repair of a half-written last entry, atomic compaction with rename.
6. **Proof:** unit tests, a randomized test against std::set, end-to-end tests, sanitizers in CI, benchmarked against real Redis at roughly 70–100%+ of its throughput depending on the workload.

### ⏱ 5 minutes: the full walkthrough (project deep-dive rounds)
| Time | Section | What to say or show |
|---|---|---|
| 0:00–0:30 | What and why | 15-second version + why I built it (to learn how real servers work) |
| 0:30–1:30 | Live demo or picture | `redis-cli` against my server: SET/GET, ZADD/ZRANGE, a WRONGTYPE error, restart and the data is back |
| 1:30–2:45 | Request flow | the diagram in Part 2, pointing at each box |
| 2:45–3:30 | Numbers | benchmark table: level with Redis on simple cases, slower with many keys, and *why* (rehashing, allocator) |
| 3:30–4:40 | The 5 KB bug | measure → surprise → cause (variant size) → fix → regression test |
| 4:40–5:00 | Close | "Happy to go deeper into the event loop, the parser, the skip list or crash safety. Where would you like to start?" |

The interactive version of this, with screen-share mode, is `docs/interview/index.html`.

### ⏱ 10+ minutes: whiteboard deep dive (system-design-style rounds)
Have these drawings ready. Practise each on paper in under 3 minutes:
1. **The event loop:** the `while` loop with `epoll_wait` and the three cases (accept / read / write).
2. **A RESP command and the three parser answers** (Ok / Incomplete / Error), with a split packet.
3. **The skip list** with spans, computing a rank.
4. **The AOF:** the log with a half-written last entry being cut off; temp file → fsync → rename.
5. **Deployment:** browser → Caddy → ttyd → CLI → server, with which ports are public.

Then be ready for: *"How would you scale it?"* (shard keys across instances, replicas fed by the log) and *"What would you add next?"* (the limitations in Part 4).

---

## My personal contribution

**Be completely honest here.** Interviewers ask "what did *you* do?", and a vague or inflated answer is easy to spot. This is a draft based on how the project was built. **Edit it so every sentence is true for you.**

> "I first built a basic version by following a tutorial. When I tested it, I found real problems: it treated each network read as one command, used a thread per client, and had bugs in DEL and in key expiry. So I redesigned it from scratch around an event loop. I used AI assistance to write much of the new code, and I treated that like working with a collaborator: I set the scope, reviewed and tested the work, ran the benchmarks against real Redis, deployed it, and studied every part until I could explain and change it myself. Pick any file, and I'll walk you through it."

The last sentence is only true once your tracker (Part 7) is mostly 🟩. That's what makes this answer strong.

## Problems that were found and solved

These all really happened in this project. Know at least three well.

| Problem | How it was found | Cause | Fix |
|---|---|---|---|
| Every key cost ~5 KB | measuring memory with 100 000 keys | `std::variant` sized by its largest type, which held a 5 KB random generator | shared static generator, plus a size test (Feature 8) |
| First version broke with pipelined or split commands | testing beyond one command at a time | treated each `read()` as one command | input buffer + incremental parser |
| First version: `DEL` always said 0; `SET` didn't clear an old TTL | reading and testing the old code | simple bugs | rewrite + regression tests |
| Benchmark script could hang forever | running it while the server wasn't up yet | `redis-benchmark` keeps retrying a refused connection | check readiness with `redis-cli PING` |
| Service could restart forever | deployment test with a large log | replaying an AOF bigger than the memory cap gets it killed every time | systemd start limit: give up after 5 tries |
| Web terminal could be driven by another website | security test of the deployment | browsers re-send the password automatically | ttyd `-O` (check the request's origin) |
| Terminal stayed down after the server restarted | deployment test | `Requires=` stops dependents and doesn't bring them back | `Wants=` instead |

---

## 25 likely questions, with short answers

Answer in two to four sentences, then stop. If they want more, they'll ask.

**About the design**

1. **Why single-threaded?** Commands take microseconds in RAM; the slow part is waiting on the network, which epoll does for every client at once. One thread means no locks, no races, and atomic commands. Real Redis runs commands on one thread too.

2. **What is epoll?** A Linux system call that lets one thread watch many sockets and sleep until any of them is ready. Without it, you'd need a thread per client, or keep checking every socket in a loop.

3. **Level-triggered or edge-triggered?** Level-triggered: epoll keeps reporting a socket while it has unread data. I read one 16 KB chunk per event, which is fair between clients. Edge-triggered only reports changes, so you must read everything immediately or you hang.

4. **What happens if a command arrives in two packets?** The parser returns Incomplete, the bytes wait in that client's buffer, and parsing continues when the rest arrives. A unit test cuts a command at every possible byte to check this.

5. **What is pipelining and how do you support it?** Sending many commands without waiting for each reply. My loop runs every complete command in the buffer, writes the log once, and sends all replies together.

6. **Why is RESP length-prefixed?** So the parser knows where a value ends without searching inside it. That makes it fast and binary safe: values can contain anything, even newlines.

7. **Why `std::variant` for values?** So a key can only hold one type, which gives correct `TYPE` and `WRONGTYPE` behaviour automatically, with one lookup per command.

8. **Why `std::deque` for lists?** LPUSH and LPOP need constant time at the front. A vector shifts every element; my benchmark showed it over 1000× slower at 50 000 items.

9. **Why a skip list?** Fast insert and delete like a tree, plus rank and range in O(log n) using spans. `std::map` can't tell you a position without walking.

10. **How do you know the skip list is correct?** A randomized test runs 20 000 random operations and compares against `std::set` every 500 steps, under AddressSanitizer.

11. **How does expiry work?** Lazily on every lookup, and actively 10 times a second from an index sorted by expiry time, at most 200 keys per round.

**About durability and failure**

12. **How does persistence work?** Every change is appended to a log in the same format clients send. At startup the log is replayed through the normal command code.

13. **What if it crashes in the middle of writing the log?** The last command is incomplete. At startup it's cut off and the server continues. Damage in the middle is treated as corruption, and the server refuses to start.

14. **What's the difference between write and fsync?** `write` puts data in the OS's memory, which survives a program crash. `fsync` forces it to the disk, which survives a power cut, but it's slow. The default fsyncs once a second.

15. **Why log `PEXPIREAT` instead of `EXPIRE`?** `EXPIRE` is relative to "now", and "now" is different when the log is replayed. The absolute time gives the same result every time.

16. **How does the log compaction stay safe?** Write a new file, fsync it, then rename it over the old one. Rename is atomic, so after a crash you have one complete file or the other.

17. **How do you shut down safely?** The signal handler only sets a flag, because that's all that's safe in a handler. The loop sees it, then flushes and fsyncs the log.

**About performance and memory**

18. **How fast is it compared to Redis?** On my benchmark machine: roughly level for simple commands on one key (74–137%), and about 67–87% with 630 000 keys, where Redis's incremental rehashing and allocator win. I quote ranges because single runs vary about 20%.

19. **Tell me about a bug you fixed.** The 5 KB-per-key memory bug (Feature 8).

20. **How much memory per key?** About 175 bytes (Redis: 93). The entry is 96 bytes; the rest is the key, the hash-table node and allocator overhead.

**About scope, judgement and you**

21. **Is it production-ready?** No. There's no authentication or TLS, no memory limit inside the server, no cap on slow clients' buffers, the rewrite blocks, and there's no replication.

22. **What would you add next?** Output buffer limits, background rewrite with `fork()`, a password, `maxmemory` with eviction, then Pub/Sub or transactions (easy, because execution is already single-threaded and atomic).

23. **How would you scale it?** Run several instances and split the keys between them (sharding). Add replicas fed by the log for reads and failover. More cores alone don't help one instance.

24. **How did you test it?** Unit tests, a randomized model test, end-to-end tests over real TCP, everything again under sanitizers, in CI on every push, plus benchmarks against real Redis.

25. **Did you build this yourself?** Use your edited "personal contribution" answer above. Then offer to walk through any file.

### When you don't know an answer
Don't guess confidently. Say: *"I haven't measured that. My guess is X, because Y. Here's how I'd check it."* That's a good answer. A confident wrong answer is a bad one.

---

# Part 6. Study plan from zero

About 2 weeks at 1–2 hours a day. If your interview is sooner, do days 1, 2, 3, 6, 8 and 12.

| Day | Topic | Read | Do | Done when you can... |
|---|---|---|---|---|
| 1 | The big picture | Part 1 and 2 of this file; `docs/1-basics.md` sections 1–3 | run the server and client; try every command type | explain what it is and draw the request flow from memory |
| 2 | Sockets and the event loop | `docs/1-basics.md` §2, §4; `server/server.cpp`; walkthrough `06-server.md` | add a `printf` in `on_readable`, connect two clients | explain why one thread works (Feature 1) |
| 3 | The protocol parser | `server/resp_parser.cpp`; walkthrough `01-protocol.md` | send a split command with `nc`; run the parser tests | explain Ok / Incomplete / Error and pipelining |
| 4 | Commands and the database | `commands.cpp`, `command_helpers.h`, `database.cpp`, one `cmd_*.cpp` | add a small command (e.g. `GETDEL`) with a test | trace `SET` from parser to database |
| 5 | Expiry | `database.cpp` (find, remove_expired) | set a short TTL and watch it vanish; read the fake-clock tests | explain lazy vs active expiry and the trade-off |
| 6 | Skip list | `docs/1-basics.md` §8; `skiplist.cpp`; walkthrough `03-skiplist.md` | draw insert with spans on paper; run the randomized test | compute a rank on paper and explain why not `std::map` |
| 7 | The memory bug | Feature 8; `docs/7-defense-guide.md` known issue #1 | re-create the bug (learning path step) and watch the test fail | tell the story in 75 seconds |
| 8 | Persistence | `aof.cpp`; walkthrough `05-aof.md` | kill the server mid-use and restart; truncate an AOF by hand | answer questions 12–16 |
| 9 | Client | `client/*`; walkthrough `07-client.md` | pipe commands into the client; read `reply_reader.cpp` | explain buffered reading and `isatty` |
| 10 | Tests and benchmarks | `tests/`; walkthrough `08-tests-and-build.md` | `make test`, `make SAN=1 test`, `make microbench` | explain the randomized test and what sanitizers catch |
| 11 | Deployment | `docs/9-deploy.md`, `docs/10-deployment-concepts.md` | deploy on your VM (or read the steps closely) | describe the deployment in 30 seconds |
| 12 | Speeches | Part 5 | record yourself doing the 45 s, 2 min and 5 min versions | do each within time, without notes |
| 13 | Mock interview | the 25 questions | have a friend (or AI) ask random questions and follow-ups | answer 20 of 25 without notes |
| 14 | Gaps | Part 7 tracker | re-study every feature below 🟩 | all resume features are 🟩 |

### How to practise speaking
- **Record yourself** on your phone. Listen back once. You'll hear filler words and unclear parts immediately.
- **Use the timer.** If the 45-second pitch takes 70 seconds, cut ideas; don't talk faster.
- **Explain to a non-programmer.** If they understand Part 1, your overview is clear enough.
- **Have someone ask "why?" three times** on any feature. That's what real interviewers do.

---

# Part 7. Mastery tracker

Tick a box only when you can do it **without looking at notes**. Update the status as you go.

The five skills:
1. **Explain:** what it does and why it exists.
2. **Trace:** follow it through the actual code.
3. **Mechanism:** explain the important technical detail.
4. **Defend:** answer failure and trade-off questions.
5. **Change:** modify, test or debug it yourself.

| Feature | 1 Explain | 2 Trace | 3 Mechanism | 4 Defend | 5 Change | Status |
|---|---|---|---|---|---|---|
| Overview and request flow | ☐ | ☐ | ☐ | ☐ | ☐ | ⬜ |
| 1. Event loop (epoll) | ☐ | ☐ | ☐ | ☐ | ☐ | ⬜ |
| 2. RESP parser and pipelining | ☐ | ☐ | ☐ | ☐ | ☐ | ⬜ |
| 3. Typed values and command table | ☐ | ☐ | ☐ | ☐ | ☐ | ⬜ |
| 4. Skip list sorted sets | ☐ | ☐ | ☐ | ☐ | ☐ | ⬜ |
| 5. AOF persistence | ☐ | ☐ | ☐ | ☐ | ☐ | ⬜ |
| 6. Key expiry | ☐ | ☐ | ☐ | ☐ | ☐ | ⬜ |
| 7. Tests, benchmarks, CI | ☐ | ☐ | ☐ | ☐ | ☐ | ⬜ |
| 8. The 5 KB memory bug | ☐ | ☐ | ☐ | ☐ | ☐ | ⬜ |
| 9. Deployment | ☐ | ☐ | ☐ | ☐ | ☐ | ⬜ |
| 45 s / 2 min / 5 min speeches | ☐ | — | — | — | — | ⬜ |

**Questions I couldn't answer** (write them here, study them next):
> _ _

---

# Appendix. Blank template

Copy this for Tessera, CortexOne or any other project. Fill it **from the repository**, one feature at a time.

```markdown
# Interview preparation: <project>

## Part 0. Verified vs unverified
| Claim | Status (✅ / ⚠️ / ❌) | How it was checked |

## Part 1. Overview (1 page)
- What is it? (2 sentences)
- What problem does it solve?
- Who would use it?
- Why is the problem interesting?
- What makes it more than a basic version? (table: basic vs mine)

## Part 2. Architecture and request flow (1–2 pages)
- Diagram
- Table: component | what it does | why it exists | files
- One request, numbered steps from start to finish
- One key sentence to remember

## Part 3. Core features (½–1 page each)
### Feature: <name>
- What:
- Why (what breaks without it):
- How (files, functions, tables, APIs):
- Small code or pseudocode:
- Example you can run:
- Important detail:
- Trade-off and the alternative:
- Likely follow-up + short answer:
- My notes:

## Part 4. Failures, edge cases, trade-offs
- Table: what fails | what happens | where in code
- What the system really guarantees
- Table: choice | alternative | why
- Known limitations

## Part 5. Interview answers
- 15 s / 45 s / 2 min / 5 min / whiteboard
- My personal contribution (honest)
- Problems found and solved (table)
- 15–25 questions with short answers

## Part 6. Study plan
## Part 7. Mastery tracker (feature × Explain/Trace/Mechanism/Defend/Change)
```

---

Related: [interactive talk track](interview/index.html) · [presenting guide](4-presenting.md) · [defense guide](7-defense-guide.md) · [learning path](0-learning-path.md) · [docs index](README.md)
