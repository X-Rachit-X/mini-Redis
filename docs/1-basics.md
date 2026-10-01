# Layer 1: The Basics

This file explains every concept the project uses, starting from zero. Read it first.
Once these ideas are clear, the code is mostly just their direct translation.

---

## 1. What is Redis?

Redis is a **key-value database that keeps everything in RAM**. You give it a key (`"user:42"`) and a value, and you can get the value back later by key. RAM is ~100,000× faster than disk, so Redis answers in microseconds. That's why it is used for:

- **Caching**: store the result of a slow database query for 60 seconds.
- **Sessions**: `SET session:abc123 <user-id> EX 3600` (the key disappears after an hour).
- **Counters**: `INCR page:views` (atomic, so no two clients ever lose an update).
- **Queues**: `RPUSH jobs <job>` on one side, `LPOP jobs` on the other.
- **Leaderboards**: `ZADD scores 1500 alice`, then `ZRANGE scores 0 9` for the top 10.

Unlike a plain hash map, the value can have a **type**. mini-redis supports four:

| Type | Like in C++ | Example commands |
|---|---|---|
| string | `std::string` | `SET`, `GET`, `INCR` |
| list | `std::deque<std::string>` | `LPUSH`, `RPOP`, `LRANGE` |
| hash | `std::unordered_map<string,string>` | `HSET`, `HGET` |
| sorted set | members ordered by a number (score) | `ZADD`, `ZRANGE`, `ZRANK` |

mini-redis is a **server** (it holds the data) plus a **client** (a program you type commands into). They talk over the network.

---

## 2. TCP sockets: how two programs talk

A **socket** is the program's end of a network connection. To the program it is just a number, a **file descriptor (fd)**, that you can `read()` from and `write()` to, like a file.

**Server side**, in order:

| Call | Meaning |
|---|---|
| `socket()` | "Give me a new TCP endpoint." Returns an fd. |
| `bind()` | "Attach it to port 6379." |
| `listen()` | "Start accepting connection requests." The kernel queues them. |
| `accept()` | "Give me the next waiting client." Returns a **new fd** for that one client. |
| `read()` / `write()` | Exchange bytes with that client. |
| `close()` | Hang up. |

**Client side**: `getaddrinfo()` (turn "localhost" into an IP address), `socket()`, `connect()`, then `send()`/`recv()`.

**TCP is a byte stream, not a message stream.** This is the most important networking fact in the project. If the client sends `"SET a 1"` and then `"GET a"`, the server might receive them:
- together in one `read()`: `"SET a 1GET a"`
- split in odd places: `"SE"` then `"T a 1GET"` then `" a"`
- or exactly as sent.

TCP only guarantees that the bytes arrive **in order, without loss**. Where one message ends is *your* problem, so we need a **protocol** that marks message boundaries. Your old server ignored this: it treated each `read()` as exactly one command, which breaks under load.

---

## 3. RESP: the Redis protocol

RESP (REdis Serialization Protocol) is how Redis clients and servers frame their messages. Every piece starts with **one type character** and lines end with `\r\n` (CRLF):

| Starts with | Type | Example bytes | Meaning |
|---|---|---|---|
| `+` | simple string | `+OK\r\n` | "OK" |
| `-` | error | `-ERR unknown command\r\n` | an error message |
| `:` | integer | `:42\r\n` | the number 42 |
| `$` | bulk string | `$5\r\nhello\r\n` | 5 bytes: "hello" |
| `$-1` | null | `$-1\r\n` | "no value" (nil) |
| `*` | array | `*2\r\n...` | 2 elements follow |

A **command** from the client is always an array of bulk strings. `SET name Alice` becomes:

```
*3\r\n          <- array with 3 elements
$3\r\nSET\r\n   <- 3 bytes: SET
$4\r\nname\r\n  <- 4 bytes: name
$5\r\nAlice\r\n <- 5 bytes: Alice
```

Why the **length prefix** (`$5`) matters:
1. **Binary safety.** The server reads exactly 5 bytes, whatever they are. A value can contain `\r\n`, spaces, or even an image file. A protocol that splits on spaces or newlines can't do this.
2. **Knowing when a message is complete.** If the server has received `*3\r\n$3\r\nSET\r\n$4\r\nna`, it knows the command isn't finished and waits. This is how we handle TCP's byte stream.

**Inline commands**: Redis also accepts a plain line like `PING\r\n`, so you can type commands by hand in `telnet`/`nc`. mini-redis supports this too.

---

## 4. Serving many clients: blocking vs non-blocking, threads vs an event loop

By default, socket calls are **blocking**: `read()` on a socket with no data **puts your thread to sleep** until data arrives. With one thread and two clients, waiting on client A means client B is ignored. There are two classic fixes.

**Option A: one thread per client** (your old server). Simple, but:
- Each thread costs memory (~8 MB of stack reserved) and scheduling time. 10,000 clients means 10,000 threads.
- All threads share the database, so it needs a **mutex**. Threads then wait on each other, and bugs like data races appear.

**Option B: an event loop** (real Redis, nginx, Node.js). **One** thread, and every socket is **non-blocking**: `read()` returns immediately with the error `EAGAIN` ("nothing right now") instead of sleeping. The thread then asks the OS: *"Tell me which of these thousands of sockets are ready."* On Linux that call is **`epoll`**:

```
epoll_create1()          -> make an "interest list"
epoll_ctl(ADD, fd, IN)   -> "watch this socket for readable data"
epoll_wait()             -> sleep until at least one watched socket is ready,
                            then return the list of ready ones
```

The loop is then just:

```
forever:
    ready = epoll_wait()
    for each ready socket:
        if it's the listening socket: accept new clients
        if readable: read bytes, run the complete commands, queue the replies
        if writable: send queued replies
```

Why this works so well for Redis:
- Each command takes **microseconds** (it's RAM). The only slow part is waiting on the network, and epoll does that waiting for all clients at once.
- **One thread means no locks.** Commands run one after another, so every command is automatically **atomic**. `INCR` can never lose an update.
- It scales to tens of thousands of connections.

The catch: **nothing may block**. A slow operation inside the loop freezes every client. That's why we never call blocking functions in the loop, and why a big `KEYS *` is "dangerous" in real Redis.

**Level-triggered epoll** (our choice) keeps reporting a socket as ready *as long as* it has data. That's forgiving: if we read only part of the data, we just get told again.

---

## 5. Pipelining

Normally a client sends one command and waits for the reply before sending the next. Each round trip costs network latency (~50 µs locally, ~50 ms across the world). With **pipelining**, the client sends 100 commands at once and then reads 100 replies, paying the latency once. The server must therefore handle **many commands in one read**, which our parser loop does.

---

## 6. Expiring keys (TTL)

`SET session abc EX 60` means "delete this in 60 seconds". Redis deletes expired keys in two ways:
- **Lazily**: whenever a key is accessed, check its expiry time first and delete it if it has passed. This is cheap, but a key nobody touches again would stay in memory forever.
- **Actively**: a background job (here, 10 times per second) removes expired keys.

We store expiry as an **absolute time** ("at Unix time 1767225600000 ms") rather than "60 s from now", so it still means the same moment after a restart.

---

## 7. Persistence: the Append-Only File (AOF)

RAM is lost when the process stops, so to survive restarts we write to disk. Two classic approaches:
- **Snapshot** (Redis RDB): periodically write the whole dataset. A crash loses everything since the last snapshot.
- **Log** (Redis AOF): append **every command that changes data** to a file. On startup, **replay** the file to rebuild the data. A crash loses at most the last moments.

mini-redis uses AOF, stored in RESP format (the same bytes a client sends), so replay reuses the normal parser and command code.

**Writing is not the same as saving to disk.** `write()` only copies data into the OS's memory (the *page cache*). The OS writes it to the physical disk later. `fsync()` forces it to disk now, but it's slow (milliseconds). That's the durability vs speed trade-off:

| `--appendfsync` | When fsync runs | If the machine loses power, you lose... |
|---|---|---|
| `always` | after every batch of writes | almost nothing (slowest) |
| `everysec` | once per second (default) | up to ~1 second of writes |
| `no` | when the OS decides | up to ~30 seconds |

(If only the *process* crashes, the OS still has the data, so nothing written is lost in any mode.)

**Rewrite / compaction.** After `INCR counter` a million times, the log has a million lines but the data is one number. `REWRITEAOF` replaces the log with the minimum commands that rebuild the current data (`SET counter 1000000`).

**Atomic file replace.** Write the new file to `appendonly.aof.rewrite.tmp`, `fsync` it, then `rename()` it over the old one. `rename()` is atomic in POSIX: anyone looking sees either the complete old file or the complete new file, never half of one.

---

## 8. Skip lists (for sorted sets)

A sorted set needs, fast: insert/remove a member, find a member's position (**rank**), and list members by position (**range**).

- A sorted array has fast rank but O(n) insert.
- A balanced tree (`std::set`/`std::map`) has O(log n) insert, but **no fast rank**: `std::distance` walks one element at a time, which is O(n).
- A **skip list** is a sorted linked list with extra "express lanes":

```
level 2:  head ---------------------------> 30 ---------------------> nil
level 1:  head -----------> 10 -----------> 30 -----------> 50 -----> nil
level 0:  head --> 5 -----> 10 ----> 20 --> 30 ----> 40 --> 50 -----> nil
```

To find 40: start at the top lane and move right while the next node is smaller than 40, otherwise drop down a lane. This skips most nodes, which gives O(log n) on average. Each node's height is chosen **randomly** (each extra level with probability 1/4), so no complex rebalancing is needed, unlike red-black trees. It's simpler to implement correctly.

**Spans.** Each link also stores how many bottom-level nodes it jumps over. Adding up the spans along the search path gives the rank, in O(log n). Real Redis does exactly this.

A sorted set also needs "what's alice's score?" in O(1), so it combines a **hash map** (member → score) with the **skip list** (ordered by score).

---

## 9. C++ features you'll meet

| Feature | Where | What it does |
|---|---|---|
| `std::variant<A,B,C,D>` | `database.h` | Holds exactly one of A/B/C/D and remembers which. `std::get_if<B>(&v)` returns a pointer if it holds a B, else `nullptr`. |
| `std::deque` | lists | Like a vector, but O(1) insert/remove at **both** ends. |
| `std::unordered_map` | keyspace, hashes | Hash table: O(1) average lookup. |
| `std::set<pair<..>>` | expiry index | A sorted tree. `begin()` is always the smallest element. |
| `std::unique_ptr` | `aof_` in server | Owns an object and deletes it automatically. Empty means "no AOF". |
| templates | `find_typed<T>` | One function written once, used for every value type. |
| function pointers | command table | `void (*)(CommandContext&, const Args&)`: a variable that holds a function. |
| `std::from_chars` / `to_chars` | number parsing | Fast, locale-free conversion between text and numbers. |
| anonymous `namespace {}` | most `.cpp` files | Makes helpers private to that file. |
| `#pragma once` | headers | Include the header only once per file (replaces `#ifndef` guards). |
| RAII | `TcpConnection`, `Aof` | The destructor closes the fd, so resources are never leaked. |

---

## 10. Signals

Pressing Ctrl+C sends **SIGINT** to the process, and `kill` sends **SIGTERM**. By default both kill it instantly. We install a handler instead, so that we can finish cleanly (flush the AOF). A handler interrupts the program *at any instruction*, so it may do almost nothing safely. Even `printf` or locking a mutex can deadlock. The safe pattern is to **set a flag** (`volatile sig_atomic_t`) and let the main loop notice it. Your old server called `exit()` and file I/O from inside the handler, which is unsafe.

**SIGPIPE**: writing to a socket whose other end has closed sends SIGPIPE, which **kills the process by default**. A server must ignore it, or a single disconnecting client could crash it.

---

Next: [Layer 2: Architecture](2-architecture.md)
