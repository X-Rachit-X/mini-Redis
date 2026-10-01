#include "commands.h"

#include "aof.h"
#include "command_helpers.h"

namespace {

// Built once, the first time it's needed (C++ guarantees this runs only once).
const CommandTable& command_table() {
    static const CommandTable table = [] {
        CommandTable t;
        register_key_commands(t);
        register_string_commands(t);
        register_list_commands(t);
        register_hash_commands(t);
        register_zset_commands(t);
        return t;
    }();
    return table;
}

bool arity_ok(int arity, size_t argc) {
    if (arity > 0) return static_cast<int>(argc) == arity;
    return static_cast<int>(argc) >= -arity;
}

}  // namespace

void execute_command(Database& db, Aof* aof, const Args& args, std::string& out) {
    if (args.empty()) return;

    // Command names are case-insensitive: "get", "GET" and "Get" all work.
    const CommandTable& table = command_table();
    auto it = table.find(to_upper(args[0]));
    if (it == table.end()) {
        reply_error(out, "ERR unknown command '" + args[0] + "'");
        return;
    }
    const Command& command = it->second;
    if (!arity_ok(command.arity, args.size())) {
        reply_error(out, "ERR wrong number of arguments for '" + to_lower(args[0]) + "' command");
        return;
    }

    CommandContext ctx{db, aof, out};
    command.handler(ctx, args);

    // Only commands that really changed something are written to the AOF.
    // e.g. "DEL missing-key" or "SET k v NX" on an existing key are not logged.
    if (ctx.dirty && aof != nullptr) aof->log_command(db, args);
}
