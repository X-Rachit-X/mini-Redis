#include "aof.h"

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <unistd.h>

#include "command_helpers.h"
#include "database.h"
#include "resp_parser.h"
#include "resp_writer.h"

namespace {

// write() may write only part of the data, so keep going until it's all out.
bool write_all(int fd, const std::string& data) {
    size_t written = 0;
    while (written < data.size()) {
        ssize_t n = ::write(fd, data.data() + written, data.size() - written);
        if (n < 0) {
            if (errno == EINTR) continue;  // interrupted by a signal: retry
            return false;
        }
        written += static_cast<size_t>(n);
    }
    return true;
}

// Appends the commands that recreate one key (used by rewrite).
void append_entry(std::string& out, const std::string& key, const Entry& entry) {
    Args args;
    if (const auto* text = std::get_if<std::string>(&entry.value)) {
        args = {"SET", key, *text};
    } else if (const auto* list = std::get_if<List>(&entry.value)) {
        args = {"RPUSH", key};
        args.insert(args.end(), list->begin(), list->end());
    } else if (const auto* hash = std::get_if<Hash>(&entry.value)) {
        args = {"HSET", key};
        for (const auto& [field, value] : *hash) {
            args.push_back(field);
            args.push_back(value);
        }
    } else if (const auto* zset = std::get_if<SortedSet>(&entry.value)) {
        args = {"ZADD", key};
        if (zset->size() > 0) {
            for (const auto& [member, score] : zset->range(0, zset->size() - 1)) {
                args.push_back(format_double(score));
                args.push_back(member);
            }
        }
    }
    if (args.size() <= 2 && args[0] != "SET") return;  // empty container: nothing to save
    out += encode_command(args);
    if (entry.expire_at != NO_EXPIRY) {
        out += encode_command({"PEXPIREAT", key, std::to_string(entry.expire_at)});
    }
}

}  // namespace

Aof::Aof(const std::string& path, FsyncPolicy policy) : path_(path), policy_(policy) {}

Aof::~Aof() {
    flush();
    if (fd_ >= 0) {
        fsync(fd_);
        ::close(fd_);
    }
}

bool Aof::open() {
    // O_APPEND: every write goes to the end of the file.
    fd_ = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd_ < 0) {
        perror(("open " + path_).c_str());
        return false;
    }
    return true;
}

void Aof::log_command(Database& db, const Args& args) {
    std::string name = to_upper(args[0]);

    // Expiry commands are relative ("in 10 seconds"). Replaying them later
    // would give the key a fresh 10 seconds, so we log the ABSOLUTE expiry
    // time instead (PEXPIREAT key <unix-ms>). Replay is then exact.
    bool has_relative_ttl = name == "EXPIRE" || name == "PEXPIRE" || name == "SET";
    if (!has_relative_ttl) {
        buffer_ += encode_command(args);
        return;
    }

    const std::string& key = args[1];
    if (name == "SET") buffer_ += encode_command({"SET", key, args[2]});  // drop EX/PX/NX/XX

    Entry* entry = db.find(key);
    if (entry == nullptr) {
        // EXPIRE with a time in the past deleted the key.
        buffer_ += encode_command({"DEL", key});
    } else if (entry->expire_at != NO_EXPIRY) {
        buffer_ += encode_command({"PEXPIREAT", key, std::to_string(entry->expire_at)});
    }
}

void Aof::flush() {
    if (buffer_.empty() || fd_ < 0) return;
    if (!write_all(fd_, buffer_)) {
        perror("AOF write");
        return;  // keep the buffer and try again on the next flush
    }
    buffer_.clear();
    if (policy_ == FsyncPolicy::Always) fsync_now();
}

void Aof::fsync_now() {
    if (fd_ >= 0) fdatasync(fd_);  // fdatasync: like fsync, but skips unneeded metadata
}

bool Aof::rewrite(Database& db) {
    flush();

    std::string data;
    for (const auto& [key, entry] : db.entries()) {
        if (!db.is_expired(entry)) append_entry(data, key, entry);
    }

    // Write to a temporary file first, then rename() it over the old one.
    // rename() is atomic: after a crash we have either the complete old file
    // or the complete new file, never a half-written one.
    std::string temp_path = path_ + ".rewrite.tmp";
    int temp_fd = ::open(temp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (temp_fd < 0) {
        perror("AOF rewrite open");
        return false;
    }
    bool ok = write_all(temp_fd, data) && fsync(temp_fd) == 0;
    ::close(temp_fd);
    if (!ok || ::rename(temp_path.c_str(), path_.c_str()) != 0) {
        perror("AOF rewrite");
        unlink(temp_path.c_str());
        return false;
    }

    // Our old fd still points at the old (now deleted) file, so reopen.
    ::close(fd_);
    fd_ = -1;
    return open();
}

bool load_aof(const std::string& path, Database& db, long& commands_loaded) {
    commands_loaded = 0;
    std::ifstream file(path, std::ios::binary);
    if (!file) return true;  // no file yet: start empty

    // Read the whole file into memory (simple; fine for a project this size).
    std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    size_t pos = 0;
    Args args;
    std::string reply;  // replies are ignored during replay
    while (pos < data.size()) {
        size_t consumed = 0;
        std::string error;
        ParseStatus status = parse_command(data.data() + pos, data.size() - pos, args, consumed, error);

        if (status == ParseStatus::Error) {
            fprintf(stderr, "AOF %s is corrupt at byte %zu: %s\n", path.c_str(), pos, error.c_str());
            return false;
        }
        if (status == ParseStatus::Incomplete) {
            // The server most likely died in the middle of a write. Cut the
            // partial command off so new commands are appended after valid data.
            fprintf(stderr, "AOF %s: removing %zu bytes of incomplete command at the end\n",
                    path.c_str(), data.size() - pos);
            if (truncate(path.c_str(), static_cast<off_t>(pos)) != 0) {
                perror("truncate");
                return false;
            }
            break;
        }

        pos += consumed;
        if (args.empty()) continue;
        reply.clear();
        execute_command(db, nullptr, args, reply);  // aof = nullptr: don't log again
        commands_loaded++;
    }
    return true;
}
