// mini-redis-server: entry point. Parses the command line and starts the server.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "config.h"
#include "server.h"

namespace {

void print_usage() {
    printf(
        "Usage: mini-redis-server [options]\n"
        "  --port <n>                      TCP port to listen on (default 6379)\n"
        "  --aof-file <path>               append-only file (default appendonly.aof)\n"
        "  --appendfsync always|everysec|no  when to fsync the AOF (default everysec)\n"
        "  --no-aof                        keep data in memory only\n"
        "  --help                          show this message\n");
}

// Returns false if the arguments are invalid.
bool parse_args(int argc, char* argv[], Config& config) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        bool has_value = i + 1 < argc;

        if (arg == "--port" && has_value) {
            config.port = std::atoi(argv[++i]);
            if (config.port <= 0 || config.port > 65535) return false;
        } else if (arg == "--aof-file" && has_value) {
            config.aof_path = argv[++i];
        } else if (arg == "--appendfsync" && has_value) {
            std::string policy = argv[++i];
            if (policy == "always") {
                config.fsync_policy = FsyncPolicy::Always;
            } else if (policy == "everysec") {
                config.fsync_policy = FsyncPolicy::EverySec;
            } else if (policy == "no") {
                config.fsync_policy = FsyncPolicy::No;
            } else {
                return false;
            }
        } else if (arg == "--no-aof") {
            config.aof_enabled = false;
        } else {
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    // Print log lines immediately even when stdout is a file or a pipe.
    setvbuf(stdout, nullptr, _IOLBF, 0);

    if (argc == 2 && std::string(argv[1]) == "--help") {
        print_usage();
        return 0;
    }
    Config config;
    if (!parse_args(argc, argv, config)) {
        print_usage();
        return 1;
    }

    install_signal_handlers();
    Server server(config);
    return server.run();
}
