// Hash commands: HSET HMSET HGET HDEL HEXISTS HLEN HKEYS HVALS HGETALL HINCRBY

#include <climits>

#include "command_helpers.h"

namespace {

// Writes field/value pairs. Returns how many fields were new, or -1 on error
// (the error reply has already been written).
long long hash_set_pairs(CommandContext& ctx, const Args& args) {
    if (args.size() % 2 != 0) {  // name + key + pairs is always even
        reply_error(ctx.out, "ERR wrong number of arguments for '" + to_lower(args[0]) + "' command");
        return -1;
    }
    bool wrong_type;
    Hash* hash = find_or_create<Hash>(ctx.db, args[1], wrong_type);
    if (wrong_type) {
        reply_error(ctx.out, ERR_WRONGTYPE);
        return -1;
    }
    long long added = 0;
    for (size_t i = 2; i < args.size(); i += 2) {
        // insert_or_assign returns {iterator, true if the field was new}
        if (hash->insert_or_assign(args[i], args[i + 1]).second) added++;
    }
    ctx.dirty = true;
    return added;
}

// HSET key field value [field value ...]  -> number of NEW fields
void cmd_hset(CommandContext& ctx, const Args& args) {
    long long added = hash_set_pairs(ctx, args);
    if (added >= 0) reply_integer(ctx.out, added);
}

// HMSET is the old name of HSET; it replies +OK instead of a count.
void cmd_hmset(CommandContext& ctx, const Args& args) {
    if (hash_set_pairs(ctx, args) >= 0) reply_simple(ctx.out, "OK");
}

void cmd_hget(CommandContext& ctx, const Args& args) {
    bool wrong_type;
    Hash* hash = find_typed<Hash>(ctx.db, args[1], wrong_type);
    if (wrong_type) {
        reply_error(ctx.out, ERR_WRONGTYPE);
        return;
    }
    if (hash != nullptr) {
        auto it = hash->find(args[2]);
        if (it != hash->end()) {
            reply_bulk(ctx.out, it->second);
            return;
        }
    }
    reply_null(ctx.out);
}

// HDEL key field [field ...]  -> number of fields removed
void cmd_hdel(CommandContext& ctx, const Args& args) {
    bool wrong_type;
    Hash* hash = find_typed<Hash>(ctx.db, args[1], wrong_type);
    if (wrong_type) {
        reply_error(ctx.out, ERR_WRONGTYPE);
        return;
    }
    long long removed = 0;
    if (hash != nullptr) {
        for (size_t i = 2; i < args.size(); i++) removed += hash->erase(args[i]);
        if (hash->empty()) ctx.db.remove(args[1]);  // empty hashes don't exist
    }
    if (removed > 0) ctx.dirty = true;
    reply_integer(ctx.out, removed);
}

void cmd_hexists(CommandContext& ctx, const Args& args) {
    bool wrong_type;
    Hash* hash = find_typed<Hash>(ctx.db, args[1], wrong_type);
    if (wrong_type) {
        reply_error(ctx.out, ERR_WRONGTYPE);
        return;
    }
    bool exists = hash != nullptr && hash->count(args[2]) > 0;
    reply_integer(ctx.out, exists ? 1 : 0);
}

void cmd_hlen(CommandContext& ctx, const Args& args) {
    bool wrong_type;
    Hash* hash = find_typed<Hash>(ctx.db, args[1], wrong_type);
    if (wrong_type) {
        reply_error(ctx.out, ERR_WRONGTYPE);
        return;
    }
    reply_integer(ctx.out, hash == nullptr ? 0 : static_cast<long long>(hash->size()));
}

// Shared by HKEYS, HVALS and HGETALL.
void hash_dump(CommandContext& ctx, const Args& args, bool with_fields, bool with_values) {
    bool wrong_type;
    Hash* hash = find_typed<Hash>(ctx.db, args[1], wrong_type);
    if (wrong_type) {
        reply_error(ctx.out, ERR_WRONGTYPE);
        return;
    }
    if (hash == nullptr) {
        reply_array_header(ctx.out, 0);
        return;
    }
    size_t per_field = (with_fields ? 1 : 0) + (with_values ? 1 : 0);
    reply_array_header(ctx.out, hash->size() * per_field);
    for (const auto& [field, value] : *hash) {
        if (with_fields) reply_bulk(ctx.out, field);
        if (with_values) reply_bulk(ctx.out, value);
    }
}

void cmd_hkeys(CommandContext& ctx, const Args& args) { hash_dump(ctx, args, true, false); }
void cmd_hvals(CommandContext& ctx, const Args& args) { hash_dump(ctx, args, false, true); }
void cmd_hgetall(CommandContext& ctx, const Args& args) { hash_dump(ctx, args, true, true); }

// HINCRBY key field delta  -> new value of the field
void cmd_hincrby(CommandContext& ctx, const Args& args) {
    long long delta;
    if (!parse_integer(args[3], delta)) {
        reply_error(ctx.out, ERR_NOT_INTEGER);
        return;
    }
    bool wrong_type;
    Hash* hash = find_or_create<Hash>(ctx.db, args[1], wrong_type);
    if (wrong_type) {
        reply_error(ctx.out, ERR_WRONGTYPE);
        return;
    }
    long long current = 0;
    auto it = hash->find(args[2]);
    if (it != hash->end() && !parse_integer(it->second, current)) {
        reply_error(ctx.out, "ERR hash value is not an integer");
        return;
    }
    if ((delta > 0 && current > LLONG_MAX - delta) || (delta < 0 && current < LLONG_MIN - delta)) {
        reply_error(ctx.out, "ERR increment or decrement would overflow");
        return;
    }
    current += delta;
    (*hash)[args[2]] = std::to_string(current);
    ctx.dirty = true;
    reply_integer(ctx.out, current);
}

}  // namespace

void register_hash_commands(CommandTable& table) {
    table["HSET"] = {cmd_hset, -4};
    table["HMSET"] = {cmd_hmset, -4};
    table["HGET"] = {cmd_hget, 3};
    table["HDEL"] = {cmd_hdel, -3};
    table["HEXISTS"] = {cmd_hexists, 3};
    table["HLEN"] = {cmd_hlen, 2};
    table["HKEYS"] = {cmd_hkeys, 2};
    table["HVALS"] = {cmd_hvals, 2};
    table["HGETALL"] = {cmd_hgetall, 2};
    table["HINCRBY"] = {cmd_hincrby, 4};
}
