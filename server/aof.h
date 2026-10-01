#pragma once

#include <string>

#include "commands.h"

class Database;

// How often the AOF file is forced from the OS cache to the disk (fsync).
enum class FsyncPolicy {
    Always,    // after every write batch: safest, slowest
    EverySec,  // once per second: lose at most ~1s of writes on power loss (default)
    No         // never, let the OS decide: fastest
};

// AOF = Append Only File. Every command that changes data is appended to
// the file in RESP format. On startup the file is replayed to rebuild the
// data. It's a log of "what happened", not a snapshot of "what is".
class Aof {
public:
    Aof(const std::string& path, FsyncPolicy policy);
    ~Aof();
    Aof(const Aof&) = delete;
    Aof& operator=(const Aof&) = delete;

    // Opens (or creates) the file for appending. Returns false on error.
    bool open();

    // Adds a data-changing command to the in-memory buffer.
    // Relative expiry commands are rewritten with absolute times (see .cpp).
    void log_command(Database& db, const Args& args);

    // Writes the buffer to the file (and fsyncs if the policy is Always).
    void flush();

    // Forces written data to disk. Called once per second for EverySec.
    void fsync_now();

    // Compacts the file: replaces the whole history with the minimum set of
    // commands that recreates the current data. Returns false on error.
    bool rewrite(Database& db);

    FsyncPolicy policy() const { return policy_; }

private:
    std::string path_;
    FsyncPolicy policy_;
    int fd_ = -1;
    std::string buffer_;  // commands not yet written to the file
};

// Replays the AOF file into `db`. A missing file is fine (empty database).
// If the file ends with a half-written command (crash during a write), that
// tail is cut off. Returns false only if the file is corrupt.
bool load_aof(const std::string& path, Database& db, long& commands_loaded);
