#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>

#include "aof.h"
#include "config.h"
#include "connection.h"
#include "database.h"

// Makes Ctrl+C (SIGINT) and `kill` (SIGTERM) stop the server cleanly,
// and ignores SIGPIPE (writing to a closed socket must not kill us).
void install_signal_handlers();

// A single-threaded, event-driven server (the same model as real Redis).
//
// One thread waits on epoll for "socket X is readable/writable" events and
// handles each one quickly without ever blocking. Because only one thread
// touches the database, no locks are needed and commands are atomic.
class Server {
public:
    explicit Server(const Config& config);
    ~Server();

    // Loads the AOF, starts listening and runs until a stop signal arrives.
    // Returns the process exit code.
    int run();

private:
    void event_loop();
    void accept_new_clients();
    bool on_readable(Connection& conn);   // returns false if the connection was closed
    bool on_writable(Connection& conn);   // returns false if the connection was closed
    void process_input(Connection& conn);
    void watch_for_writes(Connection& conn, bool enable);
    void close_connection(int fd);
    void run_background_jobs();
    void shutdown();

    Config config_;
    Database db_;
    std::unique_ptr<Aof> aof_;  // empty when AOF is disabled
    int listen_fd_ = -1;
    int epoll_fd_ = -1;
    std::unordered_map<int, Connection> connections_;  // fd -> connection
    int64_t last_expire_run_ = 0;
    int64_t last_fsync_ = 0;
};
