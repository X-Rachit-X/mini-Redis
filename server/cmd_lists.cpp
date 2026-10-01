// List commands: LPUSH RPUSH LPOP RPOP LLEN LRANGE LINDEX LSET LREM

#include "command_helpers.h"

namespace {

// LPUSH / RPUSH key value [value ...]  -> new length of the list
void push_generic(CommandContext& ctx, const Args& args, bool to_front) {
    bool wrong_type;
    List* list = find_or_create<List>(ctx.db, args[1], wrong_type);
    if (wrong_type) {
        reply_error(ctx.out, ERR_WRONGTYPE);
        return;
    }
    for (size_t i = 2; i < args.size(); i++) {
        if (to_front) {
            list->push_front(args[i]);
        } else {
            list->push_back(args[i]);
        }
    }
    ctx.dirty = true;
    reply_integer(ctx.out, static_cast<long long>(list->size()));
}

void cmd_lpush(CommandContext& ctx, const Args& args) { push_generic(ctx, args, true); }
void cmd_rpush(CommandContext& ctx, const Args& args) { push_generic(ctx, args, false); }

// LPOP / RPOP key  -> the removed element, or nil
void pop_generic(CommandContext& ctx, const Args& args, bool from_front) {
    bool wrong_type;
    List* list = find_typed<List>(ctx.db, args[1], wrong_type);
    if (wrong_type) {
        reply_error(ctx.out, ERR_WRONGTYPE);
        return;
    }
    if (list == nullptr) {
        reply_null(ctx.out);
        return;
    }
    std::string value;
    if (from_front) {
        value = std::move(list->front());
        list->pop_front();
    } else {
        value = std::move(list->back());
        list->pop_back();
    }
    if (list->empty()) ctx.db.remove(args[1]);  // like Redis: empty lists don't exist
    ctx.dirty = true;
    reply_bulk(ctx.out, value);
}

void cmd_lpop(CommandContext& ctx, const Args& args) { pop_generic(ctx, args, true); }
void cmd_rpop(CommandContext& ctx, const Args& args) { pop_generic(ctx, args, false); }

void cmd_llen(CommandContext& ctx, const Args& args) {
    bool wrong_type;
    List* list = find_typed<List>(ctx.db, args[1], wrong_type);
    if (wrong_type) {
        reply_error(ctx.out, ERR_WRONGTYPE);
        return;
    }
    reply_integer(ctx.out, list == nullptr ? 0 : static_cast<long long>(list->size()));
}

// LRANGE key start stop  (inclusive, negative indexes count from the end)
void cmd_lrange(CommandContext& ctx, const Args& args) {
    long long start, stop;
    if (!parse_integer(args[2], start) || !parse_integer(args[3], stop)) {
        reply_error(ctx.out, ERR_NOT_INTEGER);
        return;
    }
    bool wrong_type;
    List* list = find_typed<List>(ctx.db, args[1], wrong_type);
    if (wrong_type) {
        reply_error(ctx.out, ERR_WRONGTYPE);
        return;
    }
    if (list == nullptr || !normalize_range(start, stop, static_cast<long long>(list->size()))) {
        reply_array_header(ctx.out, 0);
        return;
    }
    reply_array_header(ctx.out, stop - start + 1);
    for (long long i = start; i <= stop; i++) reply_bulk(ctx.out, (*list)[i]);
}

// Converts a possibly negative index to 0-based. Returns false if out of range.
bool resolve_index(long long& index, size_t size) {
    if (index < 0) index += static_cast<long long>(size);
    return index >= 0 && index < static_cast<long long>(size);
}

void cmd_lindex(CommandContext& ctx, const Args& args) {
    long long index;
    if (!parse_integer(args[2], index)) {
        reply_error(ctx.out, ERR_NOT_INTEGER);
        return;
    }
    bool wrong_type;
    List* list = find_typed<List>(ctx.db, args[1], wrong_type);
    if (wrong_type) {
        reply_error(ctx.out, ERR_WRONGTYPE);
    } else if (list == nullptr || !resolve_index(index, list->size())) {
        reply_null(ctx.out);
    } else {
        reply_bulk(ctx.out, (*list)[index]);
    }
}

void cmd_lset(CommandContext& ctx, const Args& args) {
    long long index;
    if (!parse_integer(args[2], index)) {
        reply_error(ctx.out, ERR_NOT_INTEGER);
        return;
    }
    bool wrong_type;
    List* list = find_typed<List>(ctx.db, args[1], wrong_type);
    if (wrong_type) {
        reply_error(ctx.out, ERR_WRONGTYPE);
    } else if (list == nullptr) {
        reply_error(ctx.out, "ERR no such key");
    } else if (!resolve_index(index, list->size())) {
        reply_error(ctx.out, "ERR index out of range");
    } else {
        (*list)[index] = args[3];
        ctx.dirty = true;
        reply_simple(ctx.out, "OK");
    }
}

// LREM key count value
//   count > 0: remove up to `count` matches, searching from the head
//   count < 0: remove up to `-count` matches, searching from the tail
//   count = 0: remove all matches
void cmd_lrem(CommandContext& ctx, const Args& args) {
    long long count;
    if (!parse_integer(args[2], count)) {
        reply_error(ctx.out, ERR_NOT_INTEGER);
        return;
    }
    bool wrong_type;
    List* list = find_typed<List>(ctx.db, args[1], wrong_type);
    if (wrong_type) {
        reply_error(ctx.out, ERR_WRONGTYPE);
        return;
    }
    if (list == nullptr) {
        reply_integer(ctx.out, 0);
        return;
    }
    const std::string& target = args[3];
    long long removed = 0;
    if (count >= 0) {
        for (auto it = list->begin(); it != list->end() && (count == 0 || removed < count);) {
            if (*it == target) {
                it = list->erase(it);  // erase returns the element after the removed one
                removed++;
            } else {
                ++it;
            }
        }
    } else {
        for (long long i = static_cast<long long>(list->size()) - 1; i >= 0 && removed < -count; i--) {
            if ((*list)[i] == target) {
                list->erase(list->begin() + i);
                removed++;
            }
        }
    }
    if (list->empty()) ctx.db.remove(args[1]);
    if (removed > 0) ctx.dirty = true;
    reply_integer(ctx.out, removed);
}

}  // namespace

void register_list_commands(CommandTable& table) {
    table["LPUSH"] = {cmd_lpush, -3};
    table["RPUSH"] = {cmd_rpush, -3};
    table["LPOP"] = {cmd_lpop, 2};
    table["RPOP"] = {cmd_rpop, 2};
    table["LLEN"] = {cmd_llen, 2};
    table["LRANGE"] = {cmd_lrange, 4};
    table["LINDEX"] = {cmd_lindex, 3};
    table["LSET"] = {cmd_lset, 4};
    table["LREM"] = {cmd_lrem, 4};
}
