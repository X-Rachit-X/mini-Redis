# 05: Persistence: `aof.h` / `aof.cpp`

---

## `server/aof.h`

```cpp
enum class FsyncPolicy { Always, EverySec, No };
```
The three durability levels. See section 7 of [the basics](../1-basics.md).

```cpp
class Aof {
public:
    Aof(const std::string& path, FsyncPolicy policy);
    ~Aof();
    Aof(const Aof&) = delete;
    Aof& operator=(const Aof&) = delete;
```
The class owns a file descriptor. Copying it would let two objects close the same fd, so copying is disabled.

```cpp
    bool open();
    void log_command(Database& db, const Args& args);
    void flush();
    void fsync_now();
    bool rewrite(Database& db);
    FsyncPolicy policy() const { return policy_; }
private:
    std::string path_;
    FsyncPolicy policy_;
    int fd_ = -1;
    std::string buffer_;
};
```
- `buffer_`: commands are first collected in memory, then written in one `write()` per event-loop batch. That's far fewer system calls than one write per command.
- `fd_ = -1`: −1 is the conventional "no file open" value (valid fds are ≥ 0).

```cpp
bool load_aof(const std::string& path, Database& db, long& commands_loaded);
```
A free function (not a member) because loading happens before the `Aof` object exists.

---

## `server/aof.cpp`

```cpp
bool write_all(int fd, const std::string& data) {
    size_t written = 0;
    while (written < data.size()) {
        ssize_t n = ::write(fd, data.data() + written, data.size() - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        written += static_cast<size_t>(n);
    }
    return true;
}
```
POSIX `write()` may write **fewer bytes than asked** (a "short write"). It's rare for files but allowed. So we loop until everything is written.
- `::write` with the leading `::` means "the global POSIX function", not some member called `write`.
- `ssize_t` is a *signed* size, because `write` returns −1 on error.
- `EINTR` means a signal interrupted the call before it wrote anything, so just retry. (Our signal handler is installed without `SA_RESTART`, so this really can happen.)

```cpp
void append_entry(std::string& out, const std::string& key, const Entry& entry) {
    Args args;
    if (const auto* text = std::get_if<std::string>(&entry.value)) {
        args = {"SET", key, *text};
    } else if (const auto* list = std::get_if<List>(&entry.value)) {
        args = {"RPUSH", key};
        args.insert(args.end(), list->begin(), list->end());
    } else if (const auto* hash = std::get_if<Hash>(&entry.value)) {
        args = {"HSET", key};
        for (const auto& [field, value] : *hash) { args.push_back(field); args.push_back(value); }
    } else if (const auto* zset = std::get_if<SortedSet>(&entry.value)) {
        args = {"ZADD", key};
        if (zset->size() > 0) {
            for (const auto& [member, score] : zset->range(0, zset->size() - 1)) {
                args.push_back(format_double(score));
                args.push_back(member);
            }
        }
    }
```
For the rewrite: turn one key into the **single** command that recreates it. `if (const auto* x = std::get_if<T>(...))` declares a variable inside the `if` condition, which is only valid in that branch. Each type maps to its natural "bulk create" command. Scores use `format_double` so they read back bit-for-bit identical.

```cpp
    if (args.size() <= 2 && args[0] != "SET") return;
```
Defensive: a container with no elements would produce `RPUSH key` with no values, which is invalid on replay. (Commands already delete empty containers, so this shouldn't happen, but persistence code should never write something it can't read back.)

```cpp
    out += encode_command(args);
    if (entry.expire_at != NO_EXPIRY) {
        out += encode_command({"PEXPIREAT", key, std::to_string(entry.expire_at)});
    }
}
```
Write the command, plus its absolute expiry if it has one.

```cpp
Aof::Aof(const std::string& path, FsyncPolicy policy) : path_(path), policy_(policy) {}

Aof::~Aof() {
    flush();
    if (fd_ >= 0) {
        fsync(fd_);
        ::close(fd_);
    }
}
```
**RAII**: whenever an `Aof` is destroyed (normal shutdown, or an early `return`), buffered commands are written, synced and the file closed. No code path can forget.

```cpp
bool Aof::open() {
    fd_ = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
```
- `O_WRONLY`: write only. `O_CREAT`: create if missing.
- `O_APPEND`: the kernel moves to the end of the file **atomically** before each write. Even if something else wrote to the file, we never overwrite existing data.
- `0644`: permissions rw-r--r-- (the leading 0 means octal).

```cpp
void Aof::log_command(Database& db, const Args& args) {
    std::string name = to_upper(args[0]);
    bool has_relative_ttl = name == "EXPIRE" || name == "PEXPIRE" || name == "SET";
    if (!has_relative_ttl) {
        buffer_ += encode_command(args);
        return;
    }
```
Most commands are logged **exactly as received**. Replaying `RPUSH l a b` always does the same thing. The exceptions are commands with *relative* times: `EXPIRE k 100` means "100 s from **now**", and "now" will be different at replay time.

```cpp
    const std::string& key = args[1];
    if (name == "SET") buffer_ += encode_command({"SET", key, args[2]});
```
For `SET`, log just `SET key value`, dropping `EX/PX/NX/XX`. NX/XX were already decided (we only get here if the SET happened, since `dirty` was true).

```cpp
    Entry* entry = db.find(key);
    if (entry == nullptr) {
        buffer_ += encode_command({"DEL", key});
    } else if (entry->expire_at != NO_EXPIRY) {
        buffer_ += encode_command({"PEXPIREAT", key, std::to_string(entry->expire_at)});
    }
}
```
**Log the result, not the request.** This runs *after* the command executed, so we look at the database:
- The key is gone (e.g. `EXPIRE k 0` deleted it), so log `DEL`.
- The key has a deadline, so log the **absolute** deadline that the command just computed.

That makes replay deterministic.

```cpp
void Aof::flush() {
    if (buffer_.empty() || fd_ < 0) return;
    if (!write_all(fd_, buffer_)) {
        perror("AOF write");
        return;
    }
    buffer_.clear();
    if (policy_ == FsyncPolicy::Always) fsync_now();
}
```
Write the batch. On failure (e.g. disk full), `perror` prints the OS reason, and the buffer is **kept** so the data isn't lost and the next flush retries. For `always`, we sync immediately.

```cpp
void Aof::fsync_now() {
    if (fd_ >= 0) fdatasync(fd_);
}
```
`fdatasync` forces file contents to the physical disk. Unlike `fsync`, it skips metadata that isn't needed to read the data back (like the last-access time), so it's a little faster.

```cpp
bool Aof::rewrite(Database& db) {
    flush();
    std::string data;
    for (const auto& [key, entry] : db.entries()) {
        if (!db.is_expired(entry)) append_entry(data, key, entry);
    }
```
First flush pending commands, so they're in the old file in case the rewrite fails. Then build the compact version of every live key in memory.

```cpp
    std::string temp_path = path_ + ".rewrite.tmp";
    int temp_fd = ::open(temp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (temp_fd < 0) { perror("AOF rewrite open"); return false; }
    bool ok = write_all(temp_fd, data) && fsync(temp_fd) == 0;
    ::close(temp_fd);
```
Write a **separate** file. `O_TRUNC` empties any leftover temp file from a previous crash. `fsync` before the rename is essential: otherwise, after a power cut, the rename could be on disk while the data isn't, leaving an empty AOF.

```cpp
    if (!ok || ::rename(temp_path.c_str(), path_.c_str()) != 0) {
        perror("AOF rewrite");
        unlink(temp_path.c_str());
        return false;
    }
```
`rename()` atomically replaces the old file. If anything failed, delete the temp file. The old AOF is still intact, so nothing is lost.

```cpp
    ::close(fd_);
    fd_ = -1;
    return open();
}
```
Subtle: our `fd_` still refers to the **old** file. On Unix a file that's been replaced keeps existing while someone has it open. Writing to it would put new commands into a file nobody will ever read. So close it and open the new one.

### Replay

```cpp
bool load_aof(const std::string& path, Database& db, long& commands_loaded) {
    commands_loaded = 0;
    std::ifstream file(path, std::ios::binary);
    if (!file) return true;
```
No file means a fresh start, which isn't an error. `binary` prevents any newline translation.

```cpp
    std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
```
A standard idiom to read a whole file into a string. The extra parentheses around the first argument avoid C++'s "most vexing parse" (it would otherwise be read as a function declaration).

```cpp
    size_t pos = 0;
    Args args;
    std::string reply;
    while (pos < data.size()) {
        size_t consumed = 0;
        std::string error;
        ParseStatus status = parse_command(data.data() + pos, data.size() - pos, args, consumed, error);
```
**Reuse the network parser.** The AOF is just a recording of the commands clients sent.

```cpp
        if (status == ParseStatus::Error) {
            fprintf(stderr, "AOF %s is corrupt at byte %zu: %s\n", ...);
            return false;
        }
```
Garbage in the middle means someone edited the file or the disk is corrupted. Refusing to start is safer than loading half the data and later overwriting the rest.

```cpp
        if (status == ParseStatus::Incomplete) {
            fprintf(stderr, "AOF %s: removing %zu bytes of incomplete command at the end\n", ...);
            if (truncate(path.c_str(), static_cast<off_t>(pos)) != 0) { perror("truncate"); return false; }
            break;
        }
```
**Crash recovery.** If power fails during a `write()`, the file may end with half a command. That's expected, not corruption. `truncate()` cuts the file at the end of the last complete command. Without this, new commands would be appended **after the garbage**, and the next startup would fail with "corrupt". (Real Redis has `aof-load-truncated yes` for the same reason.)

```cpp
        pos += consumed;
        if (args.empty()) continue;
        reply.clear();
        execute_command(db, nullptr, args, reply);
        commands_loaded++;
    }
    return true;
}
```
Run each command through the normal path, with `aof = nullptr` so replay doesn't re-log what it reads. Replies go into a scratch string and are ignored.
