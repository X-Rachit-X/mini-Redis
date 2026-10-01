// Sorted set commands: ZADD ZSCORE ZREM ZCARD ZRANK ZRANGE

#include <vector>

#include "command_helpers.h"

namespace {

// ZADD key score member [score member ...]  -> number of NEW members
void cmd_zadd(CommandContext& ctx, const Args& args) {
    if (args.size() % 2 != 0) {
        reply_error(ctx.out, ERR_SYNTAX);
        return;
    }
    // Validate every score BEFORE changing anything, so a bad score in the
    // middle doesn't leave the command half-applied.
    std::vector<double> scores;
    for (size_t i = 2; i < args.size(); i += 2) {
        double score;
        if (!parse_score(args[i], score)) {
            reply_error(ctx.out, ERR_NOT_FLOAT);
            return;
        }
        scores.push_back(score);
    }

    bool wrong_type;
    SortedSet* zset = find_or_create<SortedSet>(ctx.db, args[1], wrong_type);
    if (wrong_type) {
        reply_error(ctx.out, ERR_WRONGTYPE);
        return;
    }
    long long added = 0;
    for (size_t i = 2, n = 0; i < args.size(); i += 2, n++) {
        if (zset->add(args[i + 1], scores[n])) added++;
    }
    ctx.dirty = true;
    reply_integer(ctx.out, added);
}

void cmd_zscore(CommandContext& ctx, const Args& args) {
    bool wrong_type;
    SortedSet* zset = find_typed<SortedSet>(ctx.db, args[1], wrong_type);
    if (wrong_type) {
        reply_error(ctx.out, ERR_WRONGTYPE);
        return;
    }
    double score;
    if (zset != nullptr && zset->score(args[2], score)) {
        reply_bulk(ctx.out, format_double(score));
    } else {
        reply_null(ctx.out);
    }
}

// ZREM key member [member ...]  -> number of members removed
void cmd_zrem(CommandContext& ctx, const Args& args) {
    bool wrong_type;
    SortedSet* zset = find_typed<SortedSet>(ctx.db, args[1], wrong_type);
    if (wrong_type) {
        reply_error(ctx.out, ERR_WRONGTYPE);
        return;
    }
    long long removed = 0;
    if (zset != nullptr) {
        for (size_t i = 2; i < args.size(); i++) {
            if (zset->remove(args[i])) removed++;
        }
        if (zset->size() == 0) ctx.db.remove(args[1]);
    }
    if (removed > 0) ctx.dirty = true;
    reply_integer(ctx.out, removed);
}

void cmd_zcard(CommandContext& ctx, const Args& args) {
    bool wrong_type;
    SortedSet* zset = find_typed<SortedSet>(ctx.db, args[1], wrong_type);
    if (wrong_type) {
        reply_error(ctx.out, ERR_WRONGTYPE);
        return;
    }
    reply_integer(ctx.out, zset == nullptr ? 0 : static_cast<long long>(zset->size()));
}

// ZRANK key member  -> 0-based position by score, or nil
void cmd_zrank(CommandContext& ctx, const Args& args) {
    bool wrong_type;
    SortedSet* zset = find_typed<SortedSet>(ctx.db, args[1], wrong_type);
    if (wrong_type) {
        reply_error(ctx.out, ERR_WRONGTYPE);
        return;
    }
    long rank = zset == nullptr ? -1 : zset->rank(args[2]);
    if (rank < 0) {
        reply_null(ctx.out);
    } else {
        reply_integer(ctx.out, rank);
    }
}

// ZRANGE key start stop [WITHSCORES]
void cmd_zrange(CommandContext& ctx, const Args& args) {
    bool with_scores = false;
    if (args.size() == 5 && to_upper(args[4]) == "WITHSCORES") {
        with_scores = true;
    } else if (args.size() != 4) {
        reply_error(ctx.out, ERR_SYNTAX);
        return;
    }
    long long start, stop;
    if (!parse_integer(args[2], start) || !parse_integer(args[3], stop)) {
        reply_error(ctx.out, ERR_NOT_INTEGER);
        return;
    }
    bool wrong_type;
    SortedSet* zset = find_typed<SortedSet>(ctx.db, args[1], wrong_type);
    if (wrong_type) {
        reply_error(ctx.out, ERR_WRONGTYPE);
        return;
    }
    if (zset == nullptr || !normalize_range(start, stop, static_cast<long long>(zset->size()))) {
        reply_array_header(ctx.out, 0);
        return;
    }
    auto items = zset->range(static_cast<size_t>(start), static_cast<size_t>(stop));
    reply_array_header(ctx.out, items.size() * (with_scores ? 2 : 1));
    for (const auto& [member, score] : items) {
        reply_bulk(ctx.out, member);
        if (with_scores) reply_bulk(ctx.out, format_double(score));
    }
}

}  // namespace

void register_zset_commands(CommandTable& table) {
    table["ZADD"] = {cmd_zadd, -4};
    table["ZSCORE"] = {cmd_zscore, 3};
    table["ZREM"] = {cmd_zrem, -3};
    table["ZCARD"] = {cmd_zcard, 2};
    table["ZRANK"] = {cmd_zrank, 3};
    table["ZRANGE"] = {cmd_zrange, -4};
}
