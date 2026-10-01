# 06: Networking: `net`, `connection`, `config`, `server`, `main`

---

## `server/net.cpp`

```cpp
bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}
```
`fcntl` = "file control". Read the current flags, add `O_NONBLOCK` with bitwise OR (keeping the existing flags), and write them back. After this, `read`/`accept`/`send` return `-1` with `errno = EAGAIN` instead of sleeping.

```cpp
void set_tcp_nodelay(int fd) {
    int yes = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
}
```
**Nagle's algorithm** makes TCP hold small packets for a while, hoping to merge them, which adds up to ~40 ms of latency to tiny replies like `+OK`. Request/response servers always disable it. Real Redis does too.

```cpp
int create_listen_socket(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
```
`AF_INET` = IPv4, `SOCK_STREAM` = TCP. `0` = default protocol.

```cpp
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
```
After a server stops, its port stays in `TIME_WAIT` for ~60 s, and `bind()` would fail with "Address already in use". `SO_REUSEADDR` allows an immediate restart.

```cpp
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
```
- `{}` zero-initializes the struct (important: it has padding fields).
- `htons`/`htonl` = "host to network short/long": convert to **big-endian** byte order, which network protocols use. x86 is little-endian, so the bytes really do get swapped.
- `INADDR_ANY` listens on all network interfaces.

```cpp
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) { perror("bind"); close(fd); return -1; }
    if (listen(fd, SOMAXCONN) < 0) { ... }
    if (!set_nonblocking(fd)) { ... }
    return fd;
}
```
- `reinterpret_cast<sockaddr*>`: the socket API is C and takes a generic `sockaddr*`. We pass the IPv4-specific struct.
- `SOMAXCONN`: the largest queue of not-yet-accepted connections the OS allows.
- Every error path closes the fd before returning, so nothing leaks.
- The **listening** socket is non-blocking too, so `accept()` in a loop stops with `EAGAIN` when there's no one left.

---

## `server/connection.h`

```cpp
struct Connection {
    int fd = -1;
    std::string input;
    std::string output;
    size_t output_sent = 0;
    bool watching_writes = false;
    bool close_after_reply = false;
};
```
Per-client state:
- `input`: **the buffer that fixes the old server's main bug.** Bytes accumulate here until they form complete commands. Leftover partial bytes wait for the next read.
- `output` + `output_sent`: replies waiting to be sent, and how much of them already went out. We advance an offset instead of erasing from the front on every partial send (erasing is O(n)).
- `watching_writes`: whether EPOLLOUT is currently registered, so we only call `epoll_ctl` when it changes.
- `close_after_reply`: after a protocol error, we still want the client to *receive* the error message, then we close.

## `server/config.h`

```cpp
struct Config {
    int port = 6379;
    bool aof_enabled = true;
    std::string aof_path = "appendonly.aof";
    FsyncPolicy fsync_policy = FsyncPolicy::EverySec;
};
```
Default member initializers mean a default-constructed `Config` is already the default setup, the same as Redis's defaults.

---

## `server/server.h`

```cpp
void install_signal_handlers();
```
A free function, because it's process-wide, not per-object.

```cpp
class Server {
public:
    explicit Server(const Config& config);
```
`explicit` stops C++ from silently converting a `Config` into a `Server` in odd places.

```cpp
private:
    void event_loop();
    void accept_new_clients();
    bool on_readable(Connection& conn);
    bool on_writable(Connection& conn);
    ...
    std::unique_ptr<Aof> aof_;
    std::unordered_map<int, Connection> connections_;
```
- `on_readable`/`on_writable` return `false` if they **closed** the connection. After closing, `conn` refers to a destroyed object, and the caller must not touch it. This return value is how the caller knows.
- `std::unique_ptr<Aof>`: an empty pointer means AOF is disabled. It's deleted automatically.
- `connections_` maps fd to its state, giving O(1) lookup when epoll reports an fd.

---

## `server/server.cpp`

```cpp
volatile sig_atomic_t g_stop_requested = 0;
void on_stop_signal(int) { g_stop_requested = 1; }
```
`sig_atomic_t` is an integer type guaranteed to be read and written in one step, even if interrupted by a signal. `volatile` tells the compiler the value can change "by itself" (from the handler), so it must re-read it every loop iteration instead of caching it. The handler only sets the flag. Anything more (printf, malloc, locks) is unsafe inside a signal handler.

```cpp
const size_t READ_CHUNK = 16 * 1024;
const size_t MAX_INPUT_BUFFER = 1024 * 1024 * 1024;
const int MAX_EVENTS = 128;
const int EPOLL_TIMEOUT_MS = 100;
const int EXPIRE_INTERVAL_MS = 100;
const int MAX_EXPIRE_PER_RUN = 200;
const int FSYNC_INTERVAL_MS = 1000;
```
Every tuning number has a name, and each is explained in a comment in the source. `EPOLL_TIMEOUT_MS = 100` guarantees the loop wakes at least 10 times per second, even with no traffic, to run expiry and fsync.

```cpp
void install_signal_handlers() {
    struct sigaction action {};
    action.sa_handler = on_stop_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
    signal(SIGPIPE, SIG_IGN);
}
```
- `sigaction` is the modern, portable way to install handlers (`signal()` behaves differently across systems).
- `sa_mask` empty means no extra signals are blocked during the handler.
- `sa_flags = 0` (no `SA_RESTART`): a blocking call like `epoll_wait` is **interrupted** with `EINTR` when the signal arrives, so the loop reacts immediately.
- `SIG_IGN` for SIGPIPE: writing to a disconnected client must not kill the server.

```cpp
Server::~Server() { shutdown(); }
```
RAII again. `shutdown()` is safe to call twice (it checks `listen_fd_ < 0`).

### run()

```cpp
int Server::run() {
    if (config_.aof_enabled) {
        long loaded = 0;
        if (!load_aof(config_.aof_path, db_, loaded)) {
            fprintf(stderr, "Refusing to start. Fix or remove %s\n", ...);
            return 1;
        }
        printf("Loaded %ld commands from %s\n", loaded, ...);
        aof_ = std::make_unique<Aof>(config_.aof_path, config_.fsync_policy);
        if (!aof_->open()) return 1;
    }
```
Replay first, then open for appending. `std::make_unique` creates the object and its owner in one step.

```cpp
    listen_fd_ = create_listen_socket(config_.port);
    if (listen_fd_ < 0) return 1;
    epoll_fd_ = epoll_create1(0);
    ...
    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = listen_fd_;
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &event);
```
Create the epoll instance and register the listening socket for `EPOLLIN`. For a listening socket, "readable" means "a client is waiting to be accepted". `event.data.fd` is stored by the kernel and handed back to us with each event, so we know which socket it's about.

```cpp
    event_loop();
    shutdown();
    return 0;
}
```

### event_loop()

```cpp
    epoll_event events[MAX_EVENTS];
    last_expire_run_ = last_fsync_ = now_ms();
    while (!g_stop_requested) {
        int count = epoll_wait(epoll_fd_, events, MAX_EVENTS, EPOLL_TIMEOUT_MS);
```
**The heart of the server.** Sleep until at least one socket is ready or 100 ms pass. The kernel fills `events[0..count)`. We get up to 128 at a time; if more are ready, the next call returns them.

```cpp
        if (count < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }
```
`EINTR` means a signal arrived. `continue` jumps back to the `while`, which checks the stop flag.

```cpp
        for (int i = 0; i < count; i++) {
            int fd = events[i].data.fd;
            uint32_t flags = events[i].events;
            if (fd == listen_fd_) { accept_new_clients(); continue; }
```
Dispatch on which socket it is.

```cpp
            auto it = connections_.find(fd);
            if (it == connections_.end()) continue;
            Connection& conn = it->second;
```
The connection might have been closed while handling an earlier event in this same batch, so check it still exists.

```cpp
            if (flags & EPOLLERR) { close_connection(fd); continue; }
            if (flags & (EPOLLIN | EPOLLHUP)) {
                if (!on_readable(conn)) continue;
            }
            if (flags & EPOLLOUT) on_writable(conn);
        }
        run_background_jobs();
    }
```
- `flags & X` tests one bit of the bitmask.
- `EPOLLHUP` (the peer hung up) is handled like readable: `read()` then returns 0 and we close normally. If the client sent data and *then* hung up, that data is still processed first.
- `if (!on_readable(conn)) continue;`: if it closed the connection, we must **not** touch `conn` again (that would be a use-after-free).
- Background jobs run after each batch, at most every 100 ms.

### accept_new_clients()

```cpp
    while (true) {
        int fd = accept(listen_fd_, nullptr, nullptr);
        if (fd < 0) {
            if (errno == EINTR) continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK) perror("accept");
            return;
        }
```
Accept everyone waiting (several clients may connect at once). `EAGAIN` means the queue is empty, which is the normal end of the loop. The `nullptr`s mean "I don't need the client's address".

```cpp
        set_nonblocking(fd);
        set_tcp_nodelay(fd);
        epoll_event event{};
        event.events = EPOLLIN;
        event.data.fd = fd;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event) < 0) { perror("epoll_ctl"); close(fd); continue; }
        connections_[fd].fd = fd;
    }
```
Configure the new socket, watch it for input, and create its `Connection` (`operator[]` default-constructs it).

### on_readable()

```cpp
    char buffer[READ_CHUNK];
    ssize_t n = read(conn.fd, buffer, sizeof(buffer));
    if (n == 0) { close_connection(conn.fd); return false; }
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return true;
        close_connection(conn.fd);
        return false;
    }
```
- **One `read()` per event**, a fairness decision: epoll is level-triggered, so if more data remains, we'll be told again on the next loop. Meanwhile other clients get their turn.
- `read()` returning 0 means the client closed its side (end of file).
- `EAGAIN` is a spurious wake-up with nothing to read. That's harmless.
- Any other error (e.g. connection reset) means close.

```cpp
    conn.input.append(buffer, static_cast<size_t>(n));
    if (conn.input.size() > MAX_INPUT_BUFFER) { ...close...; return false; }
    process_input(conn);
    if (aof_) aof_->flush();
    return on_writable(conn);
```
Append the bytes, guard memory, run every complete command, **write the AOF before replying**, then try to send the replies immediately (usually it all goes out in one `send`).

### process_input()

```cpp
    size_t pos = 0;
    Args args;
    while (pos < conn.input.size() && !conn.close_after_reply) {
        size_t consumed = 0;
        std::string error;
        ParseStatus status = parse_command(conn.input.data() + pos, conn.input.size() - pos, args, consumed, error);
        if (status == ParseStatus::Incomplete) break;
```
`pos` walks through the buffer command by command. **This loop is what makes pipelining work.** Incomplete means the rest stays in the buffer for later.

```cpp
        if (status == ParseStatus::Error) {
            reply_error(conn.output, "ERR Protocol error: " + error);
            conn.close_after_reply = true;
            pos = conn.input.size();
            break;
        }
```
After a protocol error we can't find the start of the next command (the stream is out of sync), so we tell the client why and then disconnect. That's the same as Redis.

```cpp
        pos += consumed;
        execute_command(db_, aof_.get(), args, conn.output);
    }
    conn.input.erase(0, pos);
```
Run the command, appending its reply to the output buffer. `aof_.get()` returns the raw pointer (or `nullptr`). At the end, drop all consumed bytes **in one erase**.

### on_writable()

```cpp
    while (conn.output_sent < conn.output.size()) {
        ssize_t n = send(conn.fd, conn.output.data() + conn.output_sent,
                         conn.output.size() - conn.output_sent, MSG_NOSIGNAL);
        if (n > 0) { conn.output_sent += static_cast<size_t>(n); continue; }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        close_connection(conn.fd);
        return false;
    }
```
Send as much as the kernel accepts. `EAGAIN` means the socket's send buffer is full (the client is reading slowly), so stop and wait for EPOLLOUT. `MSG_NOSIGNAL` gives an `EPIPE` error instead of a SIGPIPE signal (belt and braces with `SIG_IGN`).

```cpp
    bool all_sent = conn.output_sent == conn.output.size();
    if (all_sent) {
        conn.output.clear();
        conn.output_sent = 0;
        if (conn.close_after_reply) { close_connection(conn.fd); return false; }
    }
    watch_for_writes(conn, !all_sent);
    return true;
```
Everything sent means reset the buffer (`clear()` keeps the allocated capacity, so the next reply needs no new allocation). Ask for EPOLLOUT only while data is still pending.

### watch_for_writes(), close_connection()

```cpp
void Server::watch_for_writes(Connection& conn, bool enable) {
    if (conn.watching_writes == enable) return;
    epoll_event event{};
    event.events = EPOLLIN;
    if (enable) event.events |= EPOLLOUT;
    event.data.fd = conn.fd;
    epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, conn.fd, &event);
    conn.watching_writes = enable;
}
```
`EPOLL_CTL_MOD` changes what we watch. The early return skips the system call when nothing changes, which is the common case.

```cpp
void Server::close_connection(int fd) {
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
    connections_.erase(fd);
}
```
Stop watching, close the socket, and destroy the state, in that order. Erasing from the map frees the buffers.

### run_background_jobs(), shutdown()

```cpp
    int64_t now = now_ms();
    if (now - last_expire_run_ >= EXPIRE_INTERVAL_MS) {
        last_expire_run_ = now;
        db_.remove_expired(MAX_EXPIRE_PER_RUN);
    }
    if (aof_ && aof_->policy() == FsyncPolicy::EverySec && now - last_fsync_ >= FSYNC_INTERVAL_MS) {
        last_fsync_ = now;
        aof_->fsync_now();
    }
```
A simple timer: "has enough time passed since last time?" Real Redis calls this `serverCron`.

```cpp
void Server::shutdown() {
    if (listen_fd_ < 0) return;
    for (auto& [fd, conn] : connections_) close(fd);
    connections_.clear();
    close(listen_fd_);
    listen_fd_ = -1;
    if (epoll_fd_ >= 0) close(epoll_fd_);
    epoll_fd_ = -1;
    if (aof_) { aof_->flush(); aof_->fsync_now(); }
}
```
Close everything and make the AOF durable. Setting fds to −1 makes a second call a no-op.

---

## `server/main.cpp`

```cpp
setvbuf(stdout, nullptr, _IOLBF, 0);
```
When stdout is a file or a pipe (logs, tests), C buffers output in 4 KB blocks, so log lines would appear late or be lost on a crash. `_IOLBF` means flush at every newline.

```cpp
bool parse_args(int argc, char* argv[], Config& config) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        bool has_value = i + 1 < argc;
        if (arg == "--port" && has_value) {
            config.port = std::atoi(argv[++i]);
            if (config.port <= 0 || config.port > 65535) return false;
        } ...
```
A hand-written option parser: `argv[0]` is the program name, so start at 1. Options with a value check that one exists, then consume it with `++i`. Unknown options return false and print the usage. Ports must be 1–65535.

```cpp
install_signal_handlers();
Server server(config);
return server.run();
```
`run()`'s return value becomes the process exit code: 0 means OK, 1 means a startup failure. Scripts and CI rely on this.
