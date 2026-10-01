#include "server.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "clock.h"
#include "commands.h"
#include "net.h"
#include "resp_parser.h"
#include "resp_writer.h"

namespace {

// Set by the signal handler, checked by the event loop.
// volatile sig_atomic_t is the only type that is safe to write in a handler.
volatile sig_atomic_t g_stop_requested = 0;

void on_stop_signal(int) { g_stop_requested = 1; }

const size_t READ_CHUNK = 16 * 1024;                  // bytes read per readable event
const size_t MAX_INPUT_BUFFER = 1024 * 1024 * 1024;   // 1 GB per client, like Redis
const int MAX_EVENTS = 128;                           // events handled per epoll_wait
const int EPOLL_TIMEOUT_MS = 100;                     // wake up at least 10x/second
const int EXPIRE_INTERVAL_MS = 100;                   // active expiry 10x/second
const int MAX_EXPIRE_PER_RUN = 200;                   // keep each expiry run short
const int FSYNC_INTERVAL_MS = 1000;                   // for appendfsync everysec

}  // namespace

void install_signal_handlers() {
    struct sigaction action {};
    action.sa_handler = on_stop_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;  // no SA_RESTART: epoll_wait returns EINTR right away
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
    signal(SIGPIPE, SIG_IGN);
}

Server::Server(const Config& config) : config_(config) {}

Server::~Server() {
    shutdown();
}

int Server::run() {
    // 1. Rebuild the data from the AOF, then keep appending to it.
    if (config_.aof_enabled) {
        long loaded = 0;
        if (!load_aof(config_.aof_path, db_, loaded)) {
            fprintf(stderr, "Refusing to start. Fix or remove %s\n", config_.aof_path.c_str());
            return 1;
        }
        printf("Loaded %ld commands from %s\n", loaded, config_.aof_path.c_str());
        aof_ = std::make_unique<Aof>(config_.aof_path, config_.fsync_policy);
        if (!aof_->open()) return 1;
    }

    // 2. Open the listening socket and register it with epoll.
    listen_fd_ = create_listen_socket(config_.bind_address, config_.port);
    if (listen_fd_ < 0) return 1;

    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) {
        perror("epoll_create1");
        return 1;
    }
    epoll_event event{};
    event.events = EPOLLIN;  // "tell me when a new client is waiting"
    event.data.fd = listen_fd_;
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &event);

    printf("mini-redis listening on %s:%d (AOF %s)\n", config_.bind_address.c_str(), config_.port,
           config_.aof_enabled ? config_.aof_path.c_str() : "disabled");

    // 3. Serve until Ctrl+C / SIGTERM.
    event_loop();
    shutdown();
    return 0;
}

void Server::event_loop() {
    epoll_event events[MAX_EVENTS];
    last_expire_run_ = last_fsync_ = now_ms();

    while (!g_stop_requested) {
        // Sleep until some socket is ready, or the timeout passes.
        int count = epoll_wait(epoll_fd_, events, MAX_EVENTS, EPOLL_TIMEOUT_MS);
        if (count < 0) {
            if (errno == EINTR) continue;  // a signal arrived: re-check the stop flag
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < count; i++) {
            int fd = events[i].data.fd;
            uint32_t flags = events[i].events;

            if (fd == listen_fd_) {
                accept_new_clients();
                continue;
            }
            auto it = connections_.find(fd);
            if (it == connections_.end()) continue;  // closed earlier in this batch
            Connection& conn = it->second;

            if (flags & EPOLLERR) {
                close_connection(fd);
                continue;
            }
            // EPOLLHUP: peer hung up. read() will then return 0 and we close.
            if (flags & (EPOLLIN | EPOLLHUP)) {
                if (!on_readable(conn)) continue;
            }
            if (flags & EPOLLOUT) on_writable(conn);
        }

        run_background_jobs();
    }
}

void Server::accept_new_clients() {
    // The listening socket is non-blocking, so accept until there's nobody left.
    while (true) {
        int fd = accept(listen_fd_, nullptr, nullptr);
        if (fd < 0) {
            if (errno == EINTR) continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK) perror("accept");
            return;
        }
        set_nonblocking(fd);
        set_tcp_nodelay(fd);

        epoll_event event{};
        event.events = EPOLLIN;
        event.data.fd = fd;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event) < 0) {
            perror("epoll_ctl");
            close(fd);
            continue;
        }
        connections_[fd].fd = fd;
    }
}

bool Server::on_readable(Connection& conn) {
    // ONE read per event. epoll is level-triggered, so if more data is
    // waiting we'll be told again on the next loop. This keeps one very busy
    // client from starving all the others.
    char buffer[READ_CHUNK];
    ssize_t n = read(conn.fd, buffer, sizeof(buffer));
    if (n == 0) {  // the client closed the connection
        close_connection(conn.fd);
        return false;
    }
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return true;
        close_connection(conn.fd);
        return false;
    }

    conn.input.append(buffer, static_cast<size_t>(n));
    if (conn.input.size() > MAX_INPUT_BUFFER) {
        fprintf(stderr, "Client fd %d exceeded the input buffer limit, closing\n", conn.fd);
        close_connection(conn.fd);
        return false;
    }

    process_input(conn);

    // Write the AOF BEFORE sending replies: a client must never see "+OK"
    // for a write that isn't in the AOF yet.
    if (aof_) aof_->flush();

    return on_writable(conn);  // try to send replies right away
}

void Server::process_input(Connection& conn) {
    // The buffer may hold zero, one, or MANY commands (pipelining), and the
    // last one may be incomplete. Parse as many complete ones as we can.
    size_t pos = 0;
    Args args;
    while (pos < conn.input.size() && !conn.close_after_reply) {
        size_t consumed = 0;
        std::string error;
        ParseStatus status = parse_command(conn.input.data() + pos, conn.input.size() - pos,
                                           args, consumed, error);
        if (status == ParseStatus::Incomplete) break;  // wait for more bytes
        if (status == ParseStatus::Error) {
            reply_error(conn.output, "ERR Protocol error: " + error);
            conn.close_after_reply = true;
            pos = conn.input.size();
            break;
        }
        pos += consumed;
        execute_command(db_, aof_.get(), args, conn.output);
    }
    // Remove everything we parsed in one go (cheaper than erasing per command).
    conn.input.erase(0, pos);
}

bool Server::on_writable(Connection& conn) {
    while (conn.output_sent < conn.output.size()) {
        // MSG_NOSIGNAL: if the client is gone, return EPIPE instead of SIGPIPE.
        ssize_t n = send(conn.fd, conn.output.data() + conn.output_sent,
                         conn.output.size() - conn.output_sent, MSG_NOSIGNAL);
        if (n > 0) {
            conn.output_sent += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;  // socket buffer full
        close_connection(conn.fd);
        return false;
    }

    bool all_sent = conn.output_sent == conn.output.size();
    if (all_sent) {
        conn.output.clear();
        conn.output_sent = 0;
        if (conn.close_after_reply) {
            close_connection(conn.fd);
            return false;
        }
    }
    // Only ask for EPOLLOUT while we have unsent data. Otherwise epoll would
    // wake us up constantly, because a socket is almost always writable.
    watch_for_writes(conn, !all_sent);
    return true;
}

void Server::watch_for_writes(Connection& conn, bool enable) {
    if (conn.watching_writes == enable) return;
    epoll_event event{};
    event.events = EPOLLIN;
    if (enable) event.events |= EPOLLOUT;
    event.data.fd = conn.fd;
    epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, conn.fd, &event);
    conn.watching_writes = enable;
}

void Server::close_connection(int fd) {
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
    connections_.erase(fd);  // the Connection object is destroyed here
}

void Server::run_background_jobs() {
    int64_t now = now_ms();
    if (now - last_expire_run_ >= EXPIRE_INTERVAL_MS) {
        last_expire_run_ = now;
        db_.remove_expired(MAX_EXPIRE_PER_RUN);
    }
    if (aof_ && aof_->policy() == FsyncPolicy::EverySec && now - last_fsync_ >= FSYNC_INTERVAL_MS) {
        last_fsync_ = now;
        aof_->fsync_now();
    }
}

void Server::shutdown() {
    if (listen_fd_ < 0) return;  // already shut down (or never started)
    printf("\nShutting down...\n");
    for (auto& [fd, conn] : connections_) close(fd);
    connections_.clear();
    close(listen_fd_);
    listen_fd_ = -1;
    if (epoll_fd_ >= 0) close(epoll_fd_);
    epoll_fd_ = -1;
    if (aof_) {
        aof_->flush();
        aof_->fsync_now();
        printf("AOF flushed to disk\n");
    }
    printf("Bye\n");
}
