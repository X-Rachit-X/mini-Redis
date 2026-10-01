// String commands: SET GET INCR DECR INCRBY DECRBY APPEND STRLEN MSET MGET

#include <climits>

#include "command_helpers.h"

namespace {

// SET key value [EX seconds | PX milliseconds] [NX | XX]
//   NX = only set if the key does NOT exist, XX = only set if it DOES exist.
void cmd_set(CommandContext& ctx, const Args& args) {
    bool only_if_missing = false;
    bool only_if_exists = false;
    int64_t expire_at = NO_EXPIRY;

    for (size_t i = 3; i < args.size(); i++) {
        std::string option = to_upper(args[i]);
        if (option == "NX") {
            only_if_missing = true;
        } else if (option == "XX") {
            only_if_exists = true;
        } else if ((option == "EX" || option == "PX") && i + 1 < args.size()) {
            long long amount;
            if (!parse_integer(args[i + 1], amount) || amount <= 0 || amount > MAX_EXPIRE_AMOUNT) {
                reply_error(ctx.out, "ERR invalid expire time in 'set' command");
                return;
            }
            expire_at = ctx.db.now() + (option == "EX" ? amount * 1000 : amount);
            i++;  // skip the number we just read
        } else {
            reply_error(ctx.out, ERR_SYNTAX);
            return;
        }
    }
    if (only_if_missing && only_if_exists) {
        reply_error(ctx.out, ERR_SYNTAX);
        return;
    }

    bool exists = ctx.db.find(args[1]) != nullptr;
    if ((only_if_missing && exists) || (only_if_exists && !exists)) {
        reply_null(ctx.out);  // condition not met: nothing was set
        return;
    }

    Entry& entry = ctx.db.create(args[1]);  // replaces any old value AND old TTL
    entry.value.emplace<std::string>(args[2]);
    if (expire_at != NO_EXPIRY) ctx.db.set_expire(args[1], expire_at);
    ctx.dirty = true;
    reply_simple(ctx.out, "OK");
}

void cmd_get(CommandContext& ctx, const Args& args) {
    bool wrong_type;
    std::string* value = find_typed<std::string>(ctx.db, args[1], wrong_type);
    if (wrong_type) {
        reply_error(ctx.out, ERR_WRONGTYPE);
    } else if (value == nullptr) {
        reply_null(ctx.out);
    } else {
        reply_bulk(ctx.out, *value);
    }
}

// Shared by INCR, DECR, INCRBY, DECRBY. A missing key counts as 0.
void incr_by(CommandContext& ctx, const std::string& key, long long delta) {
    bool wrong_type;
    std::string* value = find_typed<std::string>(ctx.db, key, wrong_type);
    if (wrong_type) {
        reply_error(ctx.out, ERR_WRONGTYPE);
        return;
    }
    long long current = 0;
    if (value != nullptr && !parse_integer(*value, current)) {
        reply_error(ctx.out, ERR_NOT_INTEGER);
        return;
    }
    // Check BEFORE adding: signed overflow is undefined behaviour in C++.
    if ((delta > 0 && current > LLONG_MAX - delta) || (delta < 0 && current < LLONG_MIN - delta)) {
        reply_error(ctx.out, "ERR increment or decrement would overflow");
        return;
    }
    current += delta;
    if (value != nullptr) {
        *value = std::to_string(current);  // update in place: keeps the TTL
    } else {
        ctx.db.create(key).value.emplace<std::string>(std::to_string(current));
    }
    ctx.dirty = true;
    reply_integer(ctx.out, current);
}

void cmd_incr(CommandContext& ctx, const Args& args) { incr_by(ctx, args[1], 1); }
void cmd_decr(CommandContext& ctx, const Args& args) { incr_by(ctx, args[1], -1); }

void cmd_incrby(CommandContext& ctx, const Args& args) {
    long long delta;
    if (!parse_integer(args[2], delta)) {
        reply_error(ctx.out, ERR_NOT_INTEGER);
        return;
    }
    incr_by(ctx, args[1], delta);
}

void cmd_decrby(CommandContext& ctx, const Args& args) {
    long long delta;
    if (!parse_integer(args[2], delta) || delta == LLONG_MIN) {  // -LLONG_MIN overflows
        reply_error(ctx.out, ERR_NOT_INTEGER);
        return;
    }
    incr_by(ctx, args[1], -delta);
}

// APPEND key value  -> length of the string after appending
void cmd_append(CommandContext& ctx, const Args& args) {
    bool wrong_type;
    std::string* value = find_or_create<std::string>(ctx.db, args[1], wrong_type);
    if (wrong_type) {
        reply_error(ctx.out, ERR_WRONGTYPE);
        return;
    }
    *value += args[2];
    ctx.dirty = true;
    reply_integer(ctx.out, static_cast<long long>(value->size()));
}

void cmd_strlen(CommandContext& ctx, const Args& args) {
    bool wrong_type;
    std::string* value = find_typed<std::string>(ctx.db, args[1], wrong_type);
    if (wrong_type) {
        reply_error(ctx.out, ERR_WRONGTYPE);
        return;
    }
    reply_integer(ctx.out, value == nullptr ? 0 : static_cast<long long>(value->size()));
}

// MSET key value [key value ...]
void cmd_mset(CommandContext& ctx, const Args& args) {
    if (args.size() % 2 == 0) {  // name + pairs is always an odd count
        reply_error(ctx.out, "ERR wrong number of arguments for 'mset' command");
        return;
    }
    for (size_t i = 1; i < args.size(); i += 2) {
        ctx.db.create(args[i]).value.emplace<std::string>(args[i + 1]);
    }
    ctx.dirty = true;
    reply_simple(ctx.out, "OK");
}

// MGET key [key ...]  -> one value (or nil) per key
void cmd_mget(CommandContext& ctx, const Args& args) {
    reply_array_header(ctx.out, args.size() - 1);
    for (size_t i = 1; i < args.size(); i++) {
        bool wrong_type;
        std::string* value = find_typed<std::string>(ctx.db, args[i], wrong_type);
        if (value == nullptr) {
            reply_null(ctx.out);  // missing OR not a string: Redis returns nil for both
        } else {
            reply_bulk(ctx.out, *value);
        }
    }
}

}  // namespace

void register_string_commands(CommandTable& table) {
    table["SET"] = {cmd_set, -3};
    table["GET"] = {cmd_get, 2};
    table["INCR"] = {cmd_incr, 2};
    table["DECR"] = {cmd_decr, 2};
    table["INCRBY"] = {cmd_incrby, 3};
    table["DECRBY"] = {cmd_decrby, 3};
    table["APPEND"] = {cmd_append, 3};
    table["STRLEN"] = {cmd_strlen, 2};
    table["MSET"] = {cmd_mset, -3};
    table["MGET"] = {cmd_mget, -2};
}
