# mini-redis

A Redis-compatible in-memory data store written from scratch in C++17, plus a command-line client.
It speaks the real Redis protocol (RESP), so the official `redis-cli` and `redis-benchmark` work with it unchanged.

```
$ ./bin/mini-redis-server --port 6379
mini-redis listening on port 6379 (AOF appendonly.aof)

$ ./bin/mini-redis-cli
127.0.0.1:6379> ZADD leaderboard 300 carol 100 alice 200 bob
(integer) 3
127.0.0.1:6379> ZRANGE leaderboard 0 -1 WITHSCORES
1) "alice"
2) "100"
3) "bob"
4) "200"
5) "carol"
6) "300"
127.0.0.1:6379> SET session abc123 EX 60
OK
127.0.0.1:6379> TTL session
(integer) 60
```

## Features

- **Event-driven networking**: a single-threaded `epoll` event loop with non-blocking sockets, the same model real Redis uses. It needs no locks and every command is atomic.
- **Incremental RESP parser**: handles commands split across TCP packets, **pipelining** (many commands in one packet), binary-safe values and inline (telnet-style) commands. Size limits protect against malicious input.
- **4 data types, 53 commands**: strings, lists (`std::deque`), hashes, and **sorted sets backed by a span-augmented skip list** (O(log n) `ZRANK`/`ZRANGE`, the same design as Redis).
- **Typed keyspace**: every key holds a single `std::variant` value, so you get correct `TYPE` replies and `WRONGTYPE` errors.
- **Key expiry**: lazy (checked on access) plus **active** (10×/second, driven by a time-ordered index).
- **AOF persistence** with `always` / `everysec` / `no` fsync policies:
  - Relative TTLs are logged as absolute timestamps, so replay is exact.
  - A truncated AOF tail (crash mid-write) is repaired on startup.
  - `REWRITEAOF` compacts the log through an atomic temp-file + `rename()`.
- **Graceful shutdown** on SIGINT/SIGTERM, using async-signal-safe handling.
- **Client**: an interactive REPL, a one-shot mode, and piped input. It has a buffered reply reader, redis-cli style output, and quote/escape parsing.
- **Tested**:
  - 31 unit tests, including a 20 000-operation randomized test comparing the skip list against `std::set`.
  - 34 end-to-end checks: pipelining, split packets, protocol errors, 20 concurrent clients, restart recovery.
  - Everything also runs under **AddressSanitizer + UBSan** in GitHub Actions CI.

## Build and run (Linux / WSL)

```bash
make                      # builds bin/mini-redis-server and bin/mini-redis-cli
./bin/mini-redis-server   # options: --port N  --aof-file PATH  --appendfsync always|everysec|no  --no-aof
./bin/mini-redis-cli      # options: -h HOST  -p PORT  [command args...]

make test                 # unit tests + end-to-end tests
make SAN=1 test           # same, with AddressSanitizer + UndefinedBehaviorSanitizer
make microbench           # data-structure micro benchmarks
make benchmark            # redis-benchmark: mini-redis vs real Redis (needs redis-tools)
```

Requires g++ with C++17 and Linux (`epoll`). On Windows, use WSL.

## Commands

| Group | Commands |
|---|---|
| Keys / server | `PING ECHO DEL EXISTS KEYS TYPE EXPIRE PEXPIRE PEXPIREAT TTL PTTL PERSIST RENAME DBSIZE FLUSHALL REWRITEAOF COMMAND CONFIG GET` |
| Strings | `SET [EX\|PX] [NX\|XX]`, `GET INCR DECR INCRBY DECRBY APPEND STRLEN MSET MGET` |
| Lists | `LPUSH RPUSH LPOP RPOP LLEN LRANGE LINDEX LSET LREM` |
| Hashes | `HSET HMSET HGET HDEL HEXISTS HLEN HKEYS HVALS HGETALL HINCRBY` |
| Sorted sets | `ZADD ZSCORE ZREM ZCARD ZRANK ZRANGE [WITHSCORES]` |

## Architecture

More diagrams (layers, classes, every flow as a sequence diagram, state machines, the skip list) are in [docs/5-diagrams.md](docs/5-diagrams.md).

```
                 ┌──────────────────────── mini-redis-server (1 thread) ───────────────────────┐
 client ──TCP──▶ │ epoll loop ─▶ Connection.input ─▶ RESP parser ─▶ command table ─▶ Database   │
                 │     ▲                                                  │            │        │
 client ◀─TCP─── │     └──────── Connection.output ◀── RESP writer ◀──────┘            ▼        │
                 │                                                       AOF buffer ─▶ disk     │
                 │ every 100 ms: active expiry      every 1 s: fsync (everysec)                 │
                 └─────────────────────────────────────────────────────────────────────────────┘
```

| File | Responsibility |
|---|---|
| `server/server.*` | epoll event loop, accepting clients, per-connection buffers, background jobs |
| `server/resp_parser.*` / `resp_writer.*` | decoding requests / encoding replies |
| `server/commands.*` + `cmd_*.cpp` | command table, arity checks, one file per data type |
| `server/database.*` | keyspace (`unordered_map<string, Entry>`), TTLs, expiry index |
| `server/skiplist.*` / `sorted_set.*` | span-augmented skip list + hash map = sorted set |
| `server/aof.*` | append-only file: logging, fsync policy, replay, rewrite |
| `client/*` | TCP connection, RESP encoder, buffered reply reader, printer, argument splitter |

## Benchmarks

**Micro benchmarks** (`make microbench`, WSL2, g++ 13 -O2):

| What | Result | Why it matters |
|---|---|---|
| 50 000 × LPUSH: `std::vector` vs `std::deque` | 2219 ms vs 2.1 ms (**~1000× faster**) | lists use a deque |
| RESP parser, 1M pipelined `SET` commands | **~39 M commands/s**, ~1.7 GB/s | parsing is never the bottleneck |
| 200 × ZRANK on 200 000 members: skip list vs `std::set` walk | 0.32 ms vs 1956 ms (**~6000× faster**) | spans give O(log n) rank |

**Server throughput**: `make benchmark` runs `redis-benchmark` against mini-redis and against real Redis with the same settings (1 and 50 clients, pipeline 1 and 16). It writes the results to `bench/results/summary.md`.

## Documentation

The [`docs/`](docs/README.md) folder explains the project in layers. **Start with the [learning path](docs/0-learning-path.md).**

0. [Learning path](docs/0-learning-path.md): what to study in which order, experiments to run and break, checkpoint questions, exercises.
1. [Basics](docs/1-basics.md): the concepts from zero (sockets, RESP, event loops, persistence, skip lists).
2. [Architecture](docs/2-architecture.md): how the pieces fit, every request flow, and every design decision with its trade-offs.
3. [Code walkthrough](docs/3-walkthrough/): every file explained line by line.
4. [Presenting it](docs/4-presenting.md): pitch, resume bullets, interview questions and answers.
5. [Diagrams](docs/5-diagrams.md): 24 architecture, sequence, state and data-structure diagrams (Mermaid).
6. [C++ concepts](docs/6-cpp-concepts.md): every C++ and POSIX feature used, in plain language, with where and why.
7. [Defense guide](docs/7-defense-guide.md): measured facts, claim-to-evidence map, alternatives considered, known issues and fixes.
8. [Reference](docs/8-reference.md): commands, AOF rules, limits, error messages, glossary.

## Possible next steps

Pub/Sub, `MULTI`/`EXEC` transactions, `maxmemory` with LRU eviction, background AOF rewrite with `fork()`, and replication.
