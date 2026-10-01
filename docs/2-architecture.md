# Layer 2: Architecture, Flows and Design Decisions

This layer shows how the pieces fit together, follows every important flow step by step, and explains *why* each decision was made. It also covers what was given up.

---

## 1. The map

```
mini-redis/
├── server/
│   ├── main.cpp              parse command-line options, start the Server
│   ├── config.h              the options (port, AOF path, fsync policy)
│   ├── server.h/.cpp         epoll event loop, connections, background jobs, shutdown
│   ├── connection.h          per-client state: input buffer, output buffer
│   ├── net.h/.cpp            socket helpers: listen socket, non-blocking, TCP_NODELAY
│   ├── resp_parser.h/.cpp    bytes -> command (incremental, pipelining-aware)
│   ├── resp_writer.h/.cpp    reply -> bytes (+OK, :1, $5 hello, *2 ...)
│   ├── commands.h/.cpp       command table + execute_command() (lookup, arity, AOF)
│   ├── command_helpers.*     shared helpers: number parsing, find_typed<T>, errors
│   ├── cmd_keys.cpp          DEL, EXPIRE, TTL, KEYS, RENAME, ... (generic commands)
│   ├── cmd_strings.cpp       SET, GET, INCR, ...
│   ├── cmd_lists.cpp         LPUSH, LRANGE, ...
│   ├── cmd_hashes.cpp        HSET, HGETALL, ...
│   ├── cmd_zsets.cpp         ZADD, ZRANGE, ...
│   ├── database.h/.cpp       the keyspace + TTL bookkeeping
│   ├── sorted_set.h/.cpp     hash map + skip list
│   ├── skiplist.h/.cpp       the skip list itself
│   ├── aof.h/.cpp            append-only file: log, flush, fsync, replay, rewrite
│   ├── glob.h/.cpp           pattern matching for KEYS
│   └── clock.h/.cpp          now_ms()
├── client/
│   ├── main.cpp              options, one-shot mode, REPL loop
│   ├── tcp_connection.*      connect (IPv4/IPv6), send_all
│   ├── resp_encoder.*        args -> RESP bytes
│   ├── reply.h               the Reply tree type
│   ├── reply_reader.*        buffered socket reader -> Reply
│   ├── reply_printer.*       Reply -> redis-cli style text
│   └── arg_splitter.*        typed line -> args (quotes, escapes)
├── tests/                    unit tests (own tiny framework) + e2e_test.sh
├── bench/                    micro_bench.cpp + run_benchmarks.sh (redis-benchmark)
├── .github/workflows/ci.yml  build + test, normal and with sanitizers
└── Makefile
```

**Layering rule:** each layer only talks to the one below it.

```
server.cpp (network)  ->  resp_parser / resp_writer (protocol)
                      ->  commands.cpp (dispatch)  ->  cmd_*.cpp (logic)  ->  database (storage)
                                                   ->  aof (durability)
```

The database knows nothing about sockets or RESP. The commands know nothing about epoll. That's what lets the unit tests call `execute_command()` directly with no network at all.

---

## 2. Flow: server startup

```
main()
 ├─ setvbuf(stdout, line-buffered)       log lines appear immediately even in files
 ├─ parse_args() -> Config
 ├─ install_signal_handlers()            SIGINT/SIGTERM -> set flag; SIGPIPE -> ignore
 └─ Server(config).run()
     ├─ load_aof(path, db)               replay every logged command into the empty db
     │    └─ truncated tail? cut it off;  corrupt? refuse to start
     ├─ aof_ = Aof(path, policy); open() (O_APPEND)
     ├─ create_listen_socket(port)       socket, SO_REUSEADDR, bind, listen, non-blocking
     ├─ epoll_create1, add listen fd (EPOLLIN)
     └─ event_loop()
```

Why load the AOF **before** listening? So no client can ever see a half-loaded database.

---

## 3. Flow: one request, start to finish

Client types `SET name Alice`:

```
CLIENT                                         SERVER
split_args("SET name Alice") -> [SET,name,Alice]
encode_command -> "*3\r\n$3\r\nSET\r\n$4\r\nname\r\n$5\r\nAlice\r\n"
send_all() ───────────── TCP ─────────────▶  epoll_wait returns: fd 7 EPOLLIN
                                              on_readable(conn 7)
                                               ├─ read() up to 16 KB -> conn.input
                                               ├─ process_input(conn)
                                               │   loop: parse_command(input+pos)
                                               │     Ok -> args = [SET,name,Alice]
                                               │     execute_command(db, aof, args, conn.output)
                                               │       ├─ table["SET"] -> {cmd_set, arity -3}
                                               │       ├─ arity ok (3 >= 3)
                                               │       ├─ cmd_set: db.create("name") = "Alice"
                                               │       │           ctx.dirty = true; out += "+OK\r\n"
                                               │       └─ dirty -> aof.log_command -> AOF buffer
                                               │     Incomplete/end -> stop
                                               │   input.erase(0, pos)   (drop parsed bytes)
                                               ├─ aof.flush()  -> write() to the file
                                               └─ on_writable(conn) -> send("+OK\r\n")
ReplyReader::read_reply  ◀───── TCP ───────────┘
format_reply -> "OK"  -> printed
```

Key points:
- **The AOF is written before the reply is sent.** A client never sees `OK` for a write that isn't in the AOF yet.
- **`process_input` loops**, so 1000 pipelined commands in one `read()` produce 1000 replies, which are then sent together in one `send()`.
- **Parsed bytes are erased once per read**, not once per command (erasing from the front of a string costs O(length)).

---

## 4. Flow: slow client / partial write

If a client sends commands but doesn't read replies, the kernel's socket send buffer fills up and `send()` returns `EAGAIN`.

```
on_writable: send() -> sent 64 KB of 200 KB, then EAGAIN
  -> output_sent = 64 KB, keep the rest
  -> watch_for_writes(true): epoll_ctl(MOD, EPOLLIN|EPOLLOUT)
...later epoll_wait: fd 7 EPOLLOUT ("there is room now")
on_writable: send the next part ... all sent
  -> output.clear(); watch_for_writes(false)   (stop EPOLLOUT)
```

Why switch EPOLLOUT on and off? A socket is writable almost all the time. With level-triggered epoll, leaving EPOLLOUT on would wake the loop constantly with nothing to do (a busy loop at 100% CPU).

---

## 5. Flow: expiry

```
SET s v PX 500    -> entry.expire_at = now + 500; expiry_index_.insert({expire_at, "s"})

Lazy:   GET s -> db.find("s") -> is_expired? -> remove("s") -> nullptr -> reply $-1
Active: every 100 ms, run_background_jobs() -> db.remove_expired(200):
          while the smallest (expire_at, key) in the index is <= now: remove(key)
```

The expiry index is a `std::set` sorted by time, so finding expired keys means looking only at the front. The work is proportional to the number of keys that **actually expired**, never to the total number of keys. The cap of 200 per run keeps one cycle short, so the server stays responsive even if a million keys expire at once. The rest are handled in the following cycles.

Every place that changes a TTL keeps the index in sync: `create` (via `remove`), `remove`, `set_expire`, `persist`, `rename`, `clear`.

---

## 6. Flow: persistence

**Logging.** `execute_command` → handler sets `ctx.dirty` → `aof.log_command(db, args)` appends RESP bytes to an in-memory buffer → `aof.flush()` after each read batch → `write()` to the file.

**Translation of relative TTLs.**
```
client: SET k v EX 100            AOF: SET k v
                                       PEXPIREAT k 1767225700000
client: EXPIRE k 10               AOF: PEXPIREAT k <absolute>
client: EXPIRE k -1 (deletes)     AOF: DEL k
```
Without this, replaying `EXPIRE k 100` an hour later would give the key a fresh 100 seconds.

**fsync.** `always`: inside `flush()`. `everysec`: `run_background_jobs()` once per second. `no`: never (the OS decides).

**Replay** (`load_aof`): read the file, run `parse_command` in a loop, call `execute_command(db, nullptr, ...)`. The `nullptr` AOF means nothing gets logged a second time.
- If the file ends mid-command (the server died in the middle of a `write()`), we get `Incomplete`, so we `truncate()` the file at the last complete command and continue.
- If we hit garbage in the middle, we get `Error`, so we refuse to start. Silently dropping data is worse than stopping.

**Rewrite** (`REWRITEAOF`): flush, then for each live key generate one command (`SET` / `RPUSH` / `HSET` / `ZADD`, plus `PEXPIREAT` if it has a TTL). Write it to a temp file, `fsync`, `rename` over the AOF, and reopen our fd. (The old fd points to the replaced, deleted file.)

---

## 7. Flow: shutdown

```
Ctrl+C -> kernel calls on_stop_signal -> g_stop_requested = 1
epoll_wait returns EINTR (no SA_RESTART) -> loop checks the flag -> exits
shutdown(): close every client fd, the listen fd and epoll; aof.flush(); fsync; "Bye"
```
Even if the signal lands while we're busy, the 100 ms epoll timeout guarantees the flag is checked soon.

---

## 8. Flow: the client

```
main: parse -h/-p; the rest is the command
TcpConnection::connect_to: getaddrinfo (IPv4+IPv6) -> try each address -> connect
if a command was given: run once, exit (exit code 1 on an error reply, useful in scripts)
else REPL: prompt only if stdin is a terminal (isatty), so piping works
  getline -> split_args (quotes/escapes) -> quit/help handled locally
  -> encode_command -> send_all -> ReplyReader::read_reply -> format_reply -> print
```
`ReplyReader` reads 16 KB at a time into its own buffer and parses from it. Your old client called `recv()` once **per byte**, which is one system call per byte.

---

## 9. Design decisions and trade-offs

| Decision | Why | What we give up |
|---|---|---|
| **Single-threaded epoll loop** instead of thread-per-client | No locks, no data races, atomic commands, scales to many connections, same as Redis | Uses one CPU core. A slow command blocks everyone. (Redis 6+ added I/O threads for this.) |
| **Level-triggered epoll, one `read()` per event** | Simple and forgiving. One busy client can't starve the others. | A few more `epoll_wait` calls than edge-triggered "read until EAGAIN" |
| **Incremental parser that re-parses from the start of an incomplete command** | Very simple code with no parser state machine. Headers are tiny and bulk bodies are never scanned (we jump by length). | A huge command arriving in many pieces re-reads its headers each time (still cheap) |
| **Size limits** (1M args, 512 MB bulk, 64 KB header, 1 GB buffer) | One bad client can't exhaust memory | Limits instead of unlimited input (Redis has the same ones) |
| **`std::variant` value + one map** instead of 3 maps (old design) | A key has exactly one type, so `TYPE` and `WRONGTYPE` are correct automatically. One lookup per command. | Must check the type on every access (`find_typed<T>`) |
| **`std::deque` for lists** | O(1) push/pop at both ends. A vector's `insert(begin())` is O(n) (≈1000× slower in our benchmark). | Slightly more memory than a vector |
| **Skip list with spans + hash map** for sorted sets | O(log n) insert/remove/rank/range, O(1) score lookup. Simpler than a balanced tree with rank. Same as Redis. | Two structures to keep in sync. Members are stored twice. |
| **Expiry index `std::set<(time,key)>`** | Active expiry costs only as much as the keys that actually expired, and it's exact | Extra memory per key with a TTL. Redis instead *samples* 20 random keys to save memory. |
| **Absolute times (`system_clock`) for TTLs** | Survive restarts. The AOF uses absolute times. | If the system clock jumps, TTLs jump too (Redis has the same issue) |
| **AOF only, no snapshots** | One mechanism. It reuses the parser and commands for replay. | Startup time grows with the log until it's rewritten |
| **AOF written before replies are sent** | A client is never told OK for data that isn't logged | — |
| **`dirty` flag decides what is logged** | `SET NX` that didn't set, or `DEL` of a missing key, isn't logged. Replay gives exactly the same state. | Every handler must set it correctly (tests cover this) |
| **Synchronous `REWRITEAOF`** | Simple and easy to reason about | Blocks the server while it runs. Redis forks a child process (copy-on-write) to do it in the background. |
| **Command table: name → {function pointer, arity}** | O(1) dispatch. Adding a command is one line. Arity is checked in one place. | — |
| **One file per data type** | Small files you can read top to bottom | — |
| **Own tiny test framework** | No dependencies. `make test` works anywhere. | Fewer features than GoogleTest |
| **Client uses blocking sockets** | It does one thing at a time, so blocking is simplest | — |

---

## 10. Old project vs new project

| Problem in the old code | How the new code handles it |
|---|---|
| Each `recv()` was treated as exactly one command, so pipelining and split packets broke | Per-connection input buffer + incremental parser |
| `del()` always returned false | `DEL` counts deleted keys (tested) |
| `SET` didn't clear an old TTL | `create()` removes the old entry and its index entry (tested) |
| `FLUSHALL` left `expiry_map` behind | `clear()` clears both |
| A key could be a string, a list and a hash at once | `std::variant`: exactly one type, plus `WRONGTYPE` errors |
| `purgeExpired()` scanned every TTL key on every GET | Sorted index plus lazy expiry: O(log n) per operation |
| `LPUSH` on `vector` was O(n) | `deque` |
| `HSET` stored only one field and always replied 1 | Many pairs, returns the count of new fields |
| The text dump broke on spaces; writes were not atomic | RESP is binary safe; rewrite uses temp file + `rename` |
| The signal handler did I/O and `exit()` | It sets a flag and the loop shuts down cleanly |
| Thread-per-client and a growing `threads` vector | epoll event loop |
| SIGPIPE could kill the server | Ignored, plus `MSG_NOSIGNAL` |
| Client called `recv()` per byte | 16 KB buffered reader |
| No tests | 31 unit tests + 34 e2e checks, sanitizers, CI |

---

Next: [Layer 3: Code walkthrough](3-walkthrough/README.md)
