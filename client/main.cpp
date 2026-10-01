// mini-redis-cli: a command-line client for mini-redis (works with real Redis too).
//
//   mini-redis-cli                     interactive mode (REPL)
//   mini-redis-cli SET name Alice      run one command and exit
//   echo "PING" | mini-redis-cli       read commands from a pipe

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>

#include "arg_splitter.h"
#include "reply_printer.h"
#include "reply_reader.h"
#include "resp_encoder.h"
#include "tcp_connection.h"

namespace {

void print_usage() {
    std::cout <<
        "Usage: mini-redis-cli [-h host] [-p port] [command [args...]]\n"
        "  -h <host>   server host (default 127.0.0.1)\n"
        "  -p <port>   server port (default 6379)\n"
        "With a command: runs it once and exits.\n"
        "Without one: interactive mode. Type 'help' for help, 'quit' to exit.\n";
}

void print_repl_help() {
    std::cout <<
        "Type any command, for example:\n"
        "  SET name \"Alice Smith\"     GET name      DEL name\n"
        "  RPUSH tasks a b c           LRANGE tasks 0 -1\n"
        "  HSET user:1 name Bob        HGETALL user:1\n"
        "  ZADD board 10 alice 20 bob  ZRANGE board 0 -1 WITHSCORES\n"
        "  SET session abc EX 60       TTL session\n"
        "Use \"quotes\" for values with spaces. 'quit' or 'exit' leaves.\n";
}

// Sends one command, waits for the reply and prints it.
// Returns false if the connection to the server is broken.
bool run_command(TcpConnection& conn, ReplyReader& reader,
                 const std::vector<std::string>& args, bool& was_error) {
    if (!conn.send_all(encode_command(args))) return false;
    Reply reply;
    if (!reader.read_reply(reply)) return false;
    std::cout << format_reply(reply) << std::endl;
    was_error = reply.type == Reply::Type::Error;
    return true;
}

std::string lowercase(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    int port = 6379;
    std::vector<std::string> command;

    // Options come first; everything after them is the command to run.
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (command.empty() && arg == "-h" && i + 1 < argc) {
            host = argv[++i];
        } else if (command.empty() && arg == "-p" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (command.empty() && arg == "--help") {
            print_usage();
            return 0;
        } else {
            command.push_back(arg);
        }
    }

    TcpConnection conn;
    if (!conn.connect_to(host, port)) return 1;
    ReplyReader reader(conn.fd());

    // One-shot mode. Exit code 1 if the server replied with an error,
    // which makes the client easy to use in shell scripts.
    if (!command.empty()) {
        bool was_error = false;
        if (!run_command(conn, reader, command, was_error)) {
            std::cerr << "Error: connection to server lost\n";
            return 1;
        }
        return was_error ? 1 : 0;
    }

    // Interactive mode. Only show a prompt when a human is typing
    // (stdin is a terminal), not when commands are piped in.
    bool interactive = isatty(STDIN_FILENO);
    std::string prompt = host + ":" + std::to_string(port) + "> ";
    std::string line;
    std::vector<std::string> args;

    while (true) {
        if (interactive) std::cout << prompt << std::flush;
        if (!std::getline(std::cin, line)) break;  // Ctrl+D or end of input

        if (!split_args(line, args)) {
            std::cout << "(error) Invalid argument(s): unclosed quote" << std::endl;
            continue;
        }
        if (args.empty()) continue;

        std::string first = lowercase(args[0]);
        if (first == "quit" || first == "exit") break;
        if (first == "help") {
            print_repl_help();
            continue;
        }

        bool was_error = false;
        if (!run_command(conn, reader, args, was_error)) {
            std::cerr << "Error: connection to server lost\n";
            return 1;
        }
    }
    return 0;
}
