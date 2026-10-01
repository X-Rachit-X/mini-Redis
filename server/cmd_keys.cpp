// Generic and server commands:
// PING ECHO DEL EXISTS KEYS TYPE EXPIRE PEXPIRE PEXPIREAT TTL PTTL PERSIST
// RENAME DBSIZE FLUSHALL REWRITEAOF COMMAND CONFIG

#include "aof.h"
#include "command_helpers.h"
#include "glob.h"

namespace {

void cmd_ping(CommandContext& ctx, const Args& args) {
    if (args.size() > 2) {
        reply_error(ctx.out, "ERR wrong number of arguments for 'ping' command");
    } else if (args.size() == 2) {
        reply_bulk(ctx.out, args[1]);  // PING hello -> "hello"
    } else {
        reply_simple(ctx.out, "PONG");
    }
}

void cmd_echo(CommandContext& ctx, const Args& args) {
    reply_bulk(ctx.out, args[1]);
}

// DEL key [key ...]  -> number of keys deleted
void cmd_del(CommandContext& ctx, const Args& args) {
    long long deleted = 0;
    for (size_t i = 1; i < args.size(); i++) {
        // find() first, so an expired key is not counted as deleted.
        if (ctx.db.find(args[i]) != nullptr && ctx.db.remove(args[i])) deleted++;
    }
    if (deleted > 0) ctx.dirty = true;
    reply_integer(ctx.out, deleted);
}

// EXISTS key [key ...]  -> how many of the keys exist
void cmd_exists(CommandContext& ctx, const Args& args) {
    long long count = 0;
    for (size_t i = 1; i < args.size(); i++) {
        if (ctx.db.find(args[i]) != nullptr) count++;
    }
    reply_integer(ctx.out, count);
}

// KEYS pattern  -> all keys matching the glob pattern
void cmd_keys(CommandContext& ctx, const Args& args) {
    std::vector<std::string> matches;
    for (const std::string& key : ctx.db.keys()) {
        if (glob_match(args[1], key)) matches.push_back(key);
    }
    reply_array_header(ctx.out, matches.size());
    for (const std::string& key : matches) reply_bulk(ctx.out, key);
}

void cmd_type(CommandContext& ctx, const Args& args) {
    Entry* entry = ctx.db.find(args[1]);
    reply_simple(ctx.out, entry == nullptr ? "none" : type_name(entry->value));
}

// Shared by EXPIRE (seconds), PEXPIRE (ms) and PEXPIREAT (absolute ms).
void expire_generic(CommandContext& ctx, const Args& args, long long unit_ms, bool absolute) {
    long long amount;
    if (!parse_integer(args[2], amount)) {
        reply_error(ctx.out, ERR_NOT_INTEGER);
        return;
    }
    if (!absolute && (amount > MAX_EXPIRE_AMOUNT || amount < -MAX_EXPIRE_AMOUNT)) {
        reply_error(ctx.out, "ERR invalid expire time in '" + to_lower(args[0]) + "' command");
        return;
    }
    int64_t at = absolute ? amount : ctx.db.now() + amount * unit_ms;
    if (!ctx.db.set_expire(args[1], at)) {
        reply_integer(ctx.out, 0);  // key does not exist
        return;
    }
    ctx.dirty = true;
    reply_integer(ctx.out, 1);
}

void cmd_expire(CommandContext& ctx, const Args& args) { expire_generic(ctx, args, 1000, false); }
void cmd_pexpire(CommandContext& ctx, const Args& args) { expire_generic(ctx, args, 1, false); }
void cmd_pexpireat(CommandContext& ctx, const Args& args) { expire_generic(ctx, args, 1, true); }

// TTL / PTTL:  -2 = key doesn't exist, -1 = key has no expiry, else time left.
void ttl_generic(CommandContext& ctx, const Args& args, bool in_ms) {
    Entry* entry = ctx.db.find(args[1]);
    if (entry == nullptr) {
        reply_integer(ctx.out, -2);
        return;
    }
    if (entry->expire_at == NO_EXPIRY) {
        reply_integer(ctx.out, -1);
        return;
    }
    long long ms_left = entry->expire_at - ctx.db.now();
    if (ms_left < 0) ms_left = 0;
    reply_integer(ctx.out, in_ms ? ms_left : (ms_left + 500) / 1000);  // round like Redis
}

void cmd_ttl(CommandContext& ctx, const Args& args) { ttl_generic(ctx, args, false); }
void cmd_pttl(CommandContext& ctx, const Args& args) { ttl_generic(ctx, args, true); }

void cmd_persist(CommandContext& ctx, const Args& args) {
    bool removed_ttl = ctx.db.persist(args[1]);
    if (removed_ttl) ctx.dirty = true;
    reply_integer(ctx.out, removed_ttl ? 1 : 0);
}

void cmd_rename(CommandContext& ctx, const Args& args) {
    if (!ctx.db.rename(args[1], args[2])) {
        reply_error(ctx.out, "ERR no such key");
        return;
    }
    ctx.dirty = true;
    reply_simple(ctx.out, "OK");
}

void cmd_dbsize(CommandContext& ctx, const Args&) {
    reply_integer(ctx.out, static_cast<long long>(ctx.db.size()));
}

void cmd_flushall(CommandContext& ctx, const Args&) {
    ctx.db.clear();
    ctx.dirty = true;
    reply_simple(ctx.out, "OK");
}

// Compacts the AOF file (see Aof::rewrite).
void cmd_rewriteaof(CommandContext& ctx, const Args&) {
    if (ctx.aof == nullptr) {
        reply_error(ctx.out, "ERR AOF is disabled");
    } else if (ctx.aof->rewrite(ctx.db)) {
        reply_simple(ctx.out, "OK");
    } else {
        reply_error(ctx.out, "ERR AOF rewrite failed, see server log");
    }
}

// redis-cli sends "COMMAND DOCS" when it starts. An empty list keeps it happy.
void cmd_command(CommandContext& ctx, const Args&) {
    reply_array_header(ctx.out, 0);
}

// redis-benchmark asks "CONFIG GET save" and "CONFIG GET appendonly" at start.
void cmd_config(CommandContext& ctx, const Args& args) {
    if (to_upper(args[1]) != "GET" || args.size() != 3) {
        reply_error(ctx.out, "ERR only 'CONFIG GET <name>' is supported");
        return;
    }
    std::string name = to_lower(args[2]);
    if (name == "save" || name == "appendonly") {
        std::string value = (name == "save") ? "" : (ctx.aof != nullptr ? "yes" : "no");
        reply_array_header(ctx.out, 2);
        reply_bulk(ctx.out, name);
        reply_bulk(ctx.out, value);
    } else {
        reply_array_header(ctx.out, 0);
    }
}

}  // namespace

void register_key_commands(CommandTable& table) {
    table["PING"] = {cmd_ping, -1};
    table["ECHO"] = {cmd_echo, 2};
    table["DEL"] = {cmd_del, -2};
    table["EXISTS"] = {cmd_exists, -2};
    table["KEYS"] = {cmd_keys, 2};
    table["TYPE"] = {cmd_type, 2};
    table["EXPIRE"] = {cmd_expire, 3};
    table["PEXPIRE"] = {cmd_pexpire, 3};
    table["PEXPIREAT"] = {cmd_pexpireat, 3};
    table["TTL"] = {cmd_ttl, 2};
    table["PTTL"] = {cmd_pttl, 2};
    table["PERSIST"] = {cmd_persist, 2};
    table["RENAME"] = {cmd_rename, 3};
    table["DBSIZE"] = {cmd_dbsize, 1};
    table["FLUSHALL"] = {cmd_flushall, -1};
    table["REWRITEAOF"] = {cmd_rewriteaof, 1};
    table["COMMAND"] = {cmd_command, -1};
    table["CONFIG"] = {cmd_config, -2};
}
