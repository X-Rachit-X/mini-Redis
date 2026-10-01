#pragma once

#include <string>
#include <unordered_map>
#include <vector>

class Aof;
class Database;

using Args = std::vector<std::string>;  // args[0] is the command name

// Everything a command handler needs.
struct CommandContext {
    Database& db;
    Aof* aof;            // nullptr when AOF is off (or while replaying the AOF)
    std::string& out;    // the RESP reply is appended here
    bool dirty = false;  // handler sets this to true if it changed any data
};

using Handler = void (*)(CommandContext& ctx, const Args& args);

struct Command {
    Handler handler;
    // Number of arguments including the command name (same rule as Redis):
    //   arity  3 -> exactly 3   (GET key -> 2, SET key value -> 3)
    //   arity -3 -> at least 3  (variadic commands like DEL, RPUSH)
    int arity;
};

using CommandTable = std::unordered_map<std::string, Command>;

// Each cmd_*.cpp file adds its commands to the table.
void register_key_commands(CommandTable& table);
void register_string_commands(CommandTable& table);
void register_list_commands(CommandTable& table);
void register_hash_commands(CommandTable& table);
void register_zset_commands(CommandTable& table);

// Looks up and runs one command, appending the RESP reply to `out`.
// If the command changed data and `aof` is not null, it is logged to the AOF.
void execute_command(Database& db, Aof* aof, const Args& args, std::string& out);
