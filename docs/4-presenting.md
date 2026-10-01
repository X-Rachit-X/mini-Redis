# Layer 4: Presenting the project

## Resume entry

> **mini-redis**: Redis-compatible in-memory data store · C++17, Linux, epoll · [github link]
> - Built a single-threaded, **epoll-based** server with an incremental, pipelining-aware **RESP** parser. It's compatible with the official `redis-cli` and `redis-benchmark`, and supports 53 commands across strings, lists, hashes and sorted sets.
> - Implemented sorted sets with a **span-augmented skip list** (O(log n) rank/range, **over 3000× faster** ZRANK than a `std::set` walk at 200k members). Added lazy plus index-driven **active TTL expiry**.
> - Added **AOF persistence** with configurable fsync, crash-truncation recovery and atomic log compaction (temp file + `rename`). Tested with 32 unit tests (including randomized model-based tests) and 35 end-to-end checks under **ASan/UBSan** in GitHub Actions CI.
> - **Cut memory per key ~30×** (5 KB → 175 B, within 2× of real Redis) after measuring it: a 5 KB random generator inside the largest `std::variant` alternative was inflating every key. Added a regression test.

Once you've run `make benchmark` (after `sudo apt install redis-tools redis-server`), add a throughput line with **your own measured numbers**, e.g.:
> - Reached **X k ops/s** (Y% of real Redis) at 50 clients, and **Z k ops/s** with pipelining, measured with `redis-benchmark`.

For reference, on a 4-core cloud VM it measured about **55 k ops/s** at 50 clients without pipelining, and **635-920 k ops/s** with pipeline 16. That's 74-137% of Redis for one hot key, and ~67-87% of Redis with ~630 000 distinct keys. Re-measure on your own machine and quote those numbers, and if you quote the comparison, say which test it was (see [Layer 7](7-defense-guide.md#throughput-vs-real-redis-redis-benchmark)).

Only put numbers you measured yourself on the resume.

## Honesty note

Your earlier version followed a tutorial. This version is a ground-up rewrite with a different architecture. If asked, say so plainly: *"I first built a basic version following a tutorial, found its limits (no TCP framing, thread-per-client, bugs in DEL/TTL), and then redesigned it from scratch."* Interviewers like that story because it shows growth. Make sure you can explain every file; that's what the walkthrough is for.

## 30-second pitch

"I built a Redis-compatible key-value server in C++. It uses a single-threaded epoll event loop like real Redis, so there are no locks and every command is atomic. It parses the Redis protocol incrementally, so it handles pipelining and TCP fragmentation, and it works with the official redis-cli and redis-benchmark tools. Sorted sets use a skip list with span counters for log-time ranking, keys can expire, and data persists through an append-only file that recovers from crashes and can be compacted atomically. It's covered by unit tests, randomized tests and end-to-end tests that run under sanitizers in CI."

## 2-minute walkthrough (follow the request)

1. **Network**: "A client connects. `epoll_wait` reports the listening socket, I `accept`, make the socket non-blocking, disable Nagle, and register it with epoll."
2. **Framing**: "Bytes arrive into a per-connection input buffer. TCP is a byte stream, so a buffer may hold half a command or fifty. The parser returns Ok, Incomplete or Error, and I loop to run every complete command. That's how pipelining works."
3. **Dispatch**: "A hash table maps the command name to a handler and an arity. Arity is checked once, centrally."
4. **Data**: "The keyspace is an `unordered_map` from key to an `Entry`: a `std::variant` of the four types plus an expiry time. A templated helper fetches the value with type checking, which gives WRONGTYPE errors for free."
5. **Durability**: "If the handler changed data, it's appended to the AOF buffer. Relative TTLs are rewritten as absolute timestamps. The buffer is written *before* replies are sent."
6. **Reply**: "Replies go to an output buffer. If the socket can't take it all, I enable EPOLLOUT until it drains, then disable it again."
7. **Background**: "Every 100 ms: active expiry from a time-ordered index, and an fsync once a second in everysec mode."

## Likely interview questions and answers

**Why single-threaded? Isn't multi-threaded faster?**
Each command takes microseconds because it's all in RAM. The bottleneck is network I/O, and epoll handles thousands of sockets from one thread. One thread means no locks, no contention, no data races, and atomic commands for free. Redis made the same choice. The cost is using one core, and a slow command blocks everyone. Redis 6 added I/O threads for socket reads and writes but still runs commands on one thread. If I needed more throughput, I'd shard: several independent instances.

**What happens if a command arrives in two TCP packets?**
The parser returns Incomplete, the bytes stay in the connection's input buffer, and parsing resumes when more arrive. My unit test checks every possible split point, and the e2e test sends half a command, pauses, then sends the rest.

**How does pipelining work in your server?**
`process_input` loops: parse, execute, append the reply, until the buffer has no complete command left. Then the AOF is flushed once and all replies go out together. A 1000-command pipeline means one read, one AOF write and one send.

**Level- vs edge-triggered epoll?**
I use level-triggered: epoll keeps reporting a socket while it has unread data. I read once per event (16 KB), which is fair across clients and forgiving. Edge-triggered only notifies on *changes*, so you must read until EAGAIN or you'll hang. It's more efficient but easier to get wrong.

**Why do you only enable EPOLLOUT sometimes?**
A socket is writable almost always. With level-triggered epoll, a permanent EPOLLOUT would wake the loop constantly, spinning at 100% CPU. I enable it only when a `send()` couldn't finish, and disable it once the buffer is empty.

**What if a client sends a 10 GB command?**
There are limits: at most 1M arguments, 512 MB per argument, 64 KB per header line, and a 1 GB input buffer per client. Beyond that, the client gets an error and is disconnected. These are the same limits as Redis.

**What if a client never reads its replies?**
Its output buffer grows. Mine is unbounded, which is a known limitation. Redis has `client-output-buffer-limit` and disconnects such clients. I'd add a cap and close the connection when it's exceeded.

**How does expiry work?**
Two ways. **Lazy**: every lookup goes through `find()`, which deletes the key if its time has passed, so no command can ever see an expired key. **Active**: 10 times per second I pop from a `std::set` ordered by (expire time, key). It's sorted, so I only touch keys that actually expired, capped at 200 per run to keep the loop responsive. Redis instead samples 20 random keys with a TTL. That avoids the extra index memory but is probabilistic.

**Why store absolute time?**
TTLs go into the AOF. "Expire in 100 s", replayed tomorrow, would be wrong. So I log `PEXPIREAT key <unix-ms>`.

**What durability guarantees do you give?**
It depends on the fsync policy. `always` fsyncs each batch before replying, so almost nothing is lost. `everysec` loses up to ~1 s on power loss. `no` lets the OS decide (~30 s). A process crash loses nothing that was written, because the data is already in the kernel page cache. The AOF is always written before the client gets its reply.

**What if the server crashes in the middle of writing the AOF?**
On startup, the parser hits an incomplete command at the end. I truncate the file to the last complete command and continue. Garbage in the *middle* is real corruption, so I refuse to start rather than silently lose data.

**How does AOF rewrite work, and why rename?**
Generate the minimal commands for the current data, write them to a temp file, fsync, `rename()` over the old file, and reopen. `rename` is atomic, so a crash at any point leaves either the old or the new complete file. Limitation: it's synchronous, so it blocks the server. Redis forks a child that writes the snapshot using copy-on-write memory, while the parent buffers new writes and appends them at the end.

**Why a skip list rather than a balanced BST?**
Same O(log n) expected complexity, much simpler code (no rotations), and with span counters I get rank and index lookups in O(log n), which `std::map` can't do. The random levels keep it balanced on average. The hash map alongside gives O(1) ZSCORE.

**How did you test the skip list?**
A randomized model test: 20 000 random adds, updates and removes with many duplicate scores, compared against `std::set` every 500 operations (full range, every rank, and a sub-range). It also runs under AddressSanitizer, which would catch any pointer bug.

**Why `std::variant`?**
One key, one type. With separate maps per type (my first version), a key could be a string and a list at the same time. The variant makes that impossible, gives TYPE and WRONGTYPE for free, and needs one hash lookup per command.

**Why `std::deque` for lists?**
LPUSH/LPOP need O(1) at the front. `vector::insert(begin())` shifts every element. My benchmark shows a vector is over 1000× slower at 50k elements. Redis uses a "quicklist" (a linked list of compact arrays) for memory efficiency.

**How do you shut down safely?**
The SIGINT/SIGTERM handler only sets a `volatile sig_atomic_t` flag. That's all that's safe in a handler. `epoll_wait` returns EINTR (no SA_RESTART), the loop sees the flag, and then shutdown flushes and fsyncs the AOF. SIGPIPE is ignored so a disconnecting client can't kill the server.

**What would you add next?**
Output buffer limits, background rewrite with `fork()`, Pub/Sub (the event loop makes it natural), MULTI/EXEC (easy, because single-threaded execution is already atomic), maxmemory with approximate LRU, and replication by streaming the AOF to replicas.

**What was the hardest bug / most interesting part?**
Pick something real you hit while studying it. Good candidates:
- The span arithmetic in skip-list insert.
- Copying the key before `remove()` in `remove_expired`: a defensive copy, because the key lives inside the element being erased (see [Layer 6](6-cpp-concepts.md#use-after-free-traps-the-code-avoids) for why it's "safe by construction, not by luck").
- Having to reopen the AOF fd after `rename`.
- Logging relative TTLs as absolute times.

## Demo script (2 minutes, live)

```bash
make && ./bin/mini-redis-server &
./bin/mini-redis-cli
> SET user:1 "Alice Smith"
> RPUSH queue job1 job2 job3
> LPOP queue
> ZADD board 300 carol 100 alice 200 bob
> ZRANGE board 0 -1 WITHSCORES
> SET session xyz EX 5
> TTL session            # wait 5 s, then GET session -> (nil)
> GET queue              # WRONGTYPE
> quit
kill %1 && ./bin/mini-redis-server &   # restart
./bin/mini-redis-cli ZRANGE board 0 -1  # data is still there
redis-benchmark -t set,get -n 100000 -q # works with the official tool
```

---

Next: [Layer 5: Diagrams](5-diagrams.md) · For evidence behind every claim, see [Layer 7: Defense guide](7-defense-guide.md)
