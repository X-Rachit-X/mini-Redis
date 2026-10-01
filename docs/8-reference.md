# Layer 8: Reference

The facts in one place: every command, every option, every limit and constant, every error message, and a glossary.
All of it is taken directly from the source, so if this page and the code ever disagree, the code wins (and please fix this page).

Contents

1. [Commands](#1-commands)
2. [How arity works](#2-how-arity-works)
3. [What gets written to the AOF](#3-what-gets-written-to-the-aof)
4. [Server and client options](#4-server-and-client-options)
5. [Limits and tuning constants](#5-limits-and-tuning-constants)
6. [Error messages](#6-error-messages)
7. [RESP cheat sheet](#7-resp-cheat-sheet)
8. [Make targets](#8-make-targets)
9. [Glossary](#9-glossary)

---

## 1. Commands

**Arity** uses the Redis rule ([section 2](#2-how-arity-works)). **AOF** says what is logged when the command changes data: "as is", "no" (read-only), or a translation.
Reply types: `+` simple string, `-` error, `:` integer, `$` bulk string (or nil), `*` array.

### Keys and server (`server/cmd_keys.cpp`)

| Command | Arity | Reply | AOF | Notes |
|---|---|---|---|---|
| `PING [message]` | -1 | `+PONG`, or `$message` | no | more than 1 argument gives an arity error |
| `ECHO message` | 2 | `$message` | no | |
| `DEL key [key ...]` | -2 | `:` number deleted | as is, only if ≥ 1 deleted | expired keys don't count |
| `EXISTS key [key ...]` | -2 | `:` how many exist | no | a repeated key counts each time |
| `KEYS pattern` | 2 | `*` matching keys | no | glob `*` and `?` only. O(n): blocks the server |
| `TYPE key` | 2 | `+string`, `+list`, `+hash`, `+zset` or `+none` | no | |
| `EXPIRE key seconds` | 3 | `:1` set, `:0` no such key | `PEXPIREAT key <abs-ms>`, or `DEL key` if the time is past | negative or zero time deletes the key |
| `PEXPIRE key ms` | 3 | same | same | |
| `PEXPIREAT key unix-ms` | 3 | same | as is | absolute time |
| `TTL key` | 2 | `:-2` no key, `:-1` no TTL, else seconds (rounded) | no | |
| `PTTL key` | 2 | same, in ms | no | |
| `PERSIST key` | 2 | `:1` removed a TTL, else `:0` | as is, if `:1` | |
| `RENAME key newkey` | 3 | `+OK`, or `-ERR no such key` | as is | moves the TTL too. O(1) value move |
| `DBSIZE` | 1 | `:` number of keys | no | may include just-expired keys (≤ ~100 ms) |
| `FLUSHALL` | -1 | `+OK` | as is | extra arguments (e.g. `ASYNC`) are ignored |
| `REWRITEAOF` | 1 | `+OK` or `-ERR ...` | (rewrites the file) | synchronous. Real Redis calls it `BGREWRITEAOF` |
| `COMMAND [...]` | -1 | `*0` (empty) | no | only so `redis-cli` starts cleanly |
| `CONFIG GET name` | -2 | `*2 name value` for `save`/`appendonly`, else `*0` | no | other subcommands give an error |

### Strings (`server/cmd_strings.cpp`)

| Command | Arity | Reply | AOF | Notes |
|---|---|---|---|---|
| `SET key value [EX s \| PX ms] [NX \| XX]` | -3 | `+OK`, or nil if NX/XX failed | `SET key value` (+ `PEXPIREAT` if a TTL was given) | removes any old TTL. EX/PX must be > 0 |
| `GET key` | 2 | `$value` or nil | no | WRONGTYPE if not a string |
| `INCR key` / `DECR key` | 2 | `:` new value | as is | missing key counts as 0. Keeps the TTL |
| `INCRBY key n` / `DECRBY key n` | 3 | `:` new value | as is | overflow is checked |
| `APPEND key value` | 3 | `:` new length | as is | creates the key if missing |
| `STRLEN key` | 2 | `:` length (0 if missing) | no | |
| `MSET key value [key value ...]` | -3 | `+OK` | as is | removes TTLs of overwritten keys |
| `MGET key [key ...]` | -2 | `*` of values or nils | no | a non-string gives nil (not an error), like Redis |

### Lists (`server/cmd_lists.cpp`), stored as `std::deque<std::string>`

| Command | Arity | Reply | AOF | Notes |
|---|---|---|---|---|
| `LPUSH key value [value ...]` | -3 | `:` new length | as is | `LPUSH k a b c` gives `c b a` |
| `RPUSH key value [value ...]` | -3 | `:` new length | as is | |
| `LPOP key` / `RPOP key` | 2 | `$value` or nil | as is | no `count` argument. Removing the last element deletes the key |
| `LLEN key` | 2 | `:` length | no | |
| `LRANGE key start stop` | 4 | `*` elements | no | inclusive, negative = from the end |
| `LINDEX key index` | 3 | `$value` or nil | no | |
| `LSET key index value` | 4 | `+OK`, or `-ERR no such key` / `-ERR index out of range` | as is | |
| `LREM key count value` | 4 | `:` removed | as is, if ≥ 1 | count > 0 from head, < 0 from tail, 0 = all |

### Hashes (`server/cmd_hashes.cpp`), stored as `std::unordered_map<std::string, std::string>`

| Command | Arity | Reply | AOF | Notes |
|---|---|---|---|---|
| `HSET key field value [field value ...]` | -4 | `:` number of **new** fields | as is | odd pair count gives an arity error |
| `HMSET key field value [...]` | -4 | `+OK` | as is | old name for `HSET` |
| `HGET key field` | 3 | `$value` or nil | no | |
| `HDEL key field [field ...]` | -3 | `:` removed | as is, if ≥ 1 | removing the last field deletes the key |
| `HEXISTS key field` | 3 | `:1` / `:0` | no | |
| `HLEN key` | 2 | `:` number of fields | no | |
| `HKEYS` / `HVALS` / `HGETALL key` | 2 | `*` | no | order is arbitrary (hash table) |
| `HINCRBY key field n` | 4 | `:` new value | as is | `-ERR hash value is not an integer` |

### Sorted sets (`server/cmd_zsets.cpp`), hash map + skip list

| Command | Arity | Reply | AOF | Notes |
|---|---|---|---|---|
| `ZADD key score member [score member ...]` | -4 | `:` number of **new** members | as is | all scores are validated first, so the command is all-or-nothing. `inf`, `+inf`, `-inf` allowed, NaN rejected |
| `ZSCORE key member` | 3 | `$score` or nil | no | shortest exact form, e.g. `1.5` |
| `ZREM key member [member ...]` | -3 | `:` removed | as is, if ≥ 1 | removing the last member deletes the key |
| `ZCARD key` | 2 | `:` size | no | |
| `ZRANK key member` | 3 | `:` 0-based rank or nil | no | ties ordered by member (byte order) |
| `ZRANGE key start stop [WITHSCORES]` | -4 | `*` members (and scores) | no | by rank only (no `BYSCORE`/`REV`) |

**Total: 18 + 10 + 9 + 10 + 6 = 53 commands.** Command names are case-insensitive.

---

## 2. How arity works

Arity is the number of arguments **including the command name** (`commands.h:24-27`, checked in `commands.cpp:22-25`):

| Arity | Meaning | Example |
|---|---|---|
| `2` | exactly 2 | `GET key` |
| `4` | exactly 4 | `LRANGE key 0 -1` |
| `-2` | at least 2 | `DEL k1 k2 k3` |
| `-3` | at least 3 | `SET key value EX 10` |

Handlers can therefore read `args[1]` up to `args[arity - 1]` without bounds checks. Extra pairing rules (MSET, HSET, ZADD pairs) are checked inside the handler.

---

## 3. What gets written to the AOF

Rules (`commands.cpp:50`, `aof.cpp:83-105`):

1. A command is logged **only if** its handler set `ctx.dirty = true`, meaning data really changed.
2. Logged **as is**, in RESP, except:

| Client sent | AOF receives |
|---|---|
| `SET k v` | `SET k v` |
| `SET k v EX 100` (or `PX`) | `SET k v` then `PEXPIREAT k <absolute-ms>` |
| `SET k v NX` (succeeded) | `SET k v` (the option is dropped; it was already decided) |
| `EXPIRE k 100` / `PEXPIRE k 100000` | `PEXPIREAT k <absolute-ms>` |
| `EXPIRE k -1` (deletes the key) | `DEL k` |
| `GET`, `TTL`, `KEYS`, ... | nothing |
| `DEL missing`, `SET k v NX` on an existing key | nothing (dirty = false) |

**After `REWRITEAOF`**, the file contains one command per live key, plus a TTL line when needed:

| Type | Command written |
|---|---|
| string | `SET key value` |
| list | `RPUSH key e1 e2 ...` |
| hash | `HSET key f1 v1 f2 v2 ...` |
| zset | `ZADD key s1 m1 s2 m2 ...` (scores in shortest round-trip form) |
| with a TTL | followed by `PEXPIREAT key <ms>` |

---

## 4. Server and client options

### `mini-redis-server`

| Flag | Default | Meaning |
|---|---|---|
| `--port N` | 6379 | TCP port (1-65535) |
| `--aof-file PATH` | `appendonly.aof` | AOF location (relative to the current directory) |
| `--appendfsync always\|everysec\|no` | `everysec` | when to `fdatasync` the AOF |
| `--no-aof` | AOF on | memory only |
| `--help` | | usage |

Any unknown flag prints the usage and exits with code 1.

### `mini-redis-cli`

| Usage | Behaviour |
|---|---|
| `mini-redis-cli` | interactive REPL (prompt shown only when stdin is a terminal) |
| `mini-redis-cli CMD args...` | run once. Exit code 1 if the reply is an error |
| `echo "PING" \| mini-redis-cli` | piped: one command per line, no prompt |
| `-h host`, `-p port` | default `127.0.0.1`, `6379`. Host may be a name or IPv4/IPv6 |
| REPL words | `help`, `quit`, `exit`. Quotes: `"..."` with `\n \r \t \" \\` escapes, `'...'` literal |

---

## 5. Limits and tuning constants

| Constant | Value | Where | Why |
|---|---|---|---|
| `MAX_ARGS` | 1 048 576 | `resp_parser.cpp:7` | arguments in one command (Redis: same) |
| `MAX_BULK_LENGTH` | 512 MB | `resp_parser.cpp:8` | size of one argument (Redis: same) |
| `MAX_LINE_LENGTH` | 64 KB | `resp_parser.cpp:9` | header line or inline command |
| `MAX_INPUT_BUFFER` | 1 GB | `server.cpp:25` | per-client unparsed bytes |
| `READ_CHUNK` | 16 KB | `server.cpp:24` | bytes per `read()` per event (fairness) |
| `MAX_EVENTS` | 128 | `server.cpp:26` | events per `epoll_wait` |
| `EPOLL_TIMEOUT_MS` | 100 | `server.cpp:27` | the loop wakes at least 10×/s |
| `EXPIRE_INTERVAL_MS` | 100 | `server.cpp:28` | active expiry frequency |
| `MAX_EXPIRE_PER_RUN` | 200 | `server.cpp:29` | caps each expiry pause |
| `FSYNC_INTERVAL_MS` | 1000 | `server.cpp:30` | `everysec` |
| `MAX_EXPIRE_AMOUNT` | 10¹² | `command_helpers.h:21` | stops `now + amount × 1000` overflowing |
| `MAX_LEVEL` | 32 | `skiplist.h:59` | skip list height cap (enough for 4³² ≈ 10¹⁹ members) |
| `LEVEL_UP_PROBABILITY` | 0.25 | `skiplist.cpp:7` | ≈1.33 levels per node on average (Redis: same) |
| RNG seed | 12345 | `skiplist.cpp:30` (`random_level`) | reproducible shapes. One generator shared by all lists |
| listen backlog | `SOMAXCONN` | `net.cpp:43` | pending connections queue |
| client read buffer | 16 KB | `client/reply_reader.cpp:23` | one `recv()` per 16 KB |

---

## 6. Error messages

The texts match real Redis, so clients that look at error prefixes behave the same.

| Message | When |
|---|---|
| `ERR unknown command '<name>'` | not in the table |
| `ERR wrong number of arguments for '<name>' command` | arity, or MSET/HSET/HMSET pairing |
| `WRONGTYPE Operation against a key holding the wrong kind of value` | type mismatch |
| `ERR value is not an integer or out of range` | integer parsing (strict: `"12abc"` fails) |
| `ERR value is not a valid float` | ZADD score (NaN or text) |
| `ERR syntax error` | bad SET option, NX with XX, odd ZADD pairs, bad ZRANGE option |
| `ERR invalid expire time in '<cmd>' command` | SET EX/PX ≤ 0 or too large; EXPIRE amount beyond ±10¹² |
| `ERR increment or decrement would overflow` | INCR family, HINCRBY |
| `ERR hash value is not an integer` | HINCRBY on a non-numeric field |
| `ERR no such key` | RENAME, LSET on a missing key |
| `ERR index out of range` | LSET |
| `ERR AOF is disabled` | REWRITEAOF with `--no-aof` |
| `ERR AOF rewrite failed, see server log` | REWRITEAOF I/O failure |
| `ERR only 'CONFIG GET <name>' is supported` | other CONFIG subcommands |
| `ERR Protocol error: <detail>` | malformed RESP. **The connection is closed after this reply** |

Protocol error details: `too big inline request`, `too big multibulk header`, `invalid multibulk length`, `expected '$', got 'X'`, `too big bulk header`, `invalid bulk length`, `bulk string not terminated by CRLF`.

---

## 7. RESP cheat sheet

```
Request (client -> server): always an array of bulk strings
  *3\r\n $3\r\nSET\r\n $4\r\nname\r\n $5\r\nAlice\r\n        (spaces added for reading)

Replies (server -> client):
  +OK\r\n                       simple string     reply_simple
  -ERR message\r\n              error             reply_error
  :42\r\n                       integer           reply_integer
  $5\r\nhello\r\n               bulk string       reply_bulk
  $-1\r\n                       nil               reply_null
  *2\r\n<reply><reply>          array             reply_array_header + replies
  *0\r\n                        empty array

Inline (telnet/nc): a plain line, words split on spaces/tabs, ends with \n or \r\n
  GET name\r\n
```

---

## 8. Make targets

| Target | What it does |
|---|---|
| `make` | builds `bin/mini-redis-server` and `bin/mini-redis-cli` (`-O2`) |
| `make test` | unit tests + end-to-end tests |
| `make unit-test` / `make e2e-test` | one of the two |
| `make SAN=1 test` | everything in `bin/san/` with ASan + UBSan (`-O1`) |
| `make microbench` | `bin/micro-bench`: deque vs vector, parser speed, ZRANK |
| `make benchmark` | `redis-benchmark` against mini-redis and real Redis, writes `bench/results/summary.md` |
| `make clean` | removes `build/` and `bin/` |

---

## 9. Glossary

| Term | Meaning |
|---|---|
| **AOF** | Append-Only File: a log of every data-changing command, replayed at startup |
| **Arity** | number of arguments a command takes, including its name |
| **Atomic** | happens completely or not at all, with nothing able to observe a halfway state |
| **Backpressure** | slowing a producer down because the consumer can't keep up |
| **Bulk string** | a RESP string with a length prefix (`$5\r\nhello\r\n`): binary safe |
| **Binary safe** | works with any bytes, including `\0` and `\r\n` |
| **CRLF** | `\r\n`, the line ending RESP uses |
| **Dirty flag** | `ctx.dirty`: set by a handler when it changed data, decides AOF logging |
| **Edge-/level-triggered** | epoll reports a change once (edge) or keeps reporting while ready (level) |
| **EAGAIN** | "would block": a non-blocking call has nothing to do right now |
| **EINTR** | a system call was interrupted by a signal; just retry |
| **epoll** | Linux API to wait for readiness on many file descriptors at once |
| **Event loop** | one thread repeatedly waiting for events and handling each one quickly |
| **fd** | file descriptor: the integer handle for a file, socket or epoll instance |
| **fsync / fdatasync** | force data from the OS page cache to the physical disk |
| **Inline command** | a plain-text command line (`GET key`), as typed in telnet |
| **Keyspace** | the set of all keys and their values |
| **Lazy / active expiry** | delete an expired key when it's accessed / by a periodic background sweep |
| **Nagle's algorithm** | TCP delaying small sends to merge them; disabled with `TCP_NODELAY` |
| **Page cache** | the kernel's in-memory copy of file data, written to disk later |
| **Pipelining** | sending many commands without waiting for each reply |
| **Rank** | 0-based position of a member in sorted order |
| **RAII** | resources are released by destructors automatically |
| **RESP** | REdis Serialization Protocol |
| **Rewrite / compaction** | replacing the AOF history with the minimum commands for the current data |
| **RSS** | Resident Set Size: how much physical memory a process is using |
| **Skip list** | a sorted linked list with random "express lanes" for O(log n) search |
| **Span** | in the skip list, how many bottom-level nodes a link jumps over |
| **Sanitizer** | compiler-inserted runtime checks for memory errors (ASan) or undefined behaviour (UBSan) |
| **SIGPIPE** | signal sent when writing to a closed socket; kills the process by default |
| **TTL** | Time To Live: how long until a key expires |
| **UB** | Undefined Behaviour: code the C++ standard gives no meaning to |
| **WRONGTYPE** | the error for using a command on a key of a different type |

---

Back to the [documentation hub](README.md).
