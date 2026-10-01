// Tests commands end-to-end through execute_command(), checking the exact
// RESP bytes a client would receive (no network involved).

#include "../server/commands.h"
#include "../server/database.h"
#include "test_framework.h"

namespace {

int64_t fake_now = 5000000;
int64_t fake_clock() { return fake_now; }

std::string run(Database& db, const Args& args) {
    std::string out;
    execute_command(db, nullptr, args, out);
    return out;
}

}  // namespace

TEST(errors_for_unknown_commands_and_bad_arity) {
    Database db;
    CHECK_EQ(run(db, {"NOPE"}), std::string("-ERR unknown command 'NOPE'\r\n"));
    CHECK_EQ(run(db, {"GET"}), std::string("-ERR wrong number of arguments for 'get' command\r\n"));
    CHECK_EQ(run(db, {"ping"}), std::string("+PONG\r\n"));  // case-insensitive
}

TEST(string_commands) {
    Database db;
    CHECK_EQ(run(db, {"SET", "k", "v"}), std::string("+OK\r\n"));
    CHECK_EQ(run(db, {"GET", "k"}), std::string("$1\r\nv\r\n"));
    CHECK_EQ(run(db, {"GET", "missing"}), std::string("$-1\r\n"));
    CHECK_EQ(run(db, {"SET", "k", "x", "NX"}), std::string("$-1\r\n"));   // exists -> not set
    CHECK_EQ(run(db, {"SET", "new", "x", "XX"}), std::string("$-1\r\n")); // missing -> not set
    CHECK_EQ(run(db, {"APPEND", "k", "ww"}), std::string(":3\r\n"));
    CHECK_EQ(run(db, {"STRLEN", "k"}), std::string(":3\r\n"));
    CHECK_EQ(run(db, {"MSET", "a", "1", "b", "2"}), std::string("+OK\r\n"));
    CHECK_EQ(run(db, {"MGET", "a", "nope", "b"}), std::string("*3\r\n$1\r\n1\r\n$-1\r\n$1\r\n2\r\n"));
    CHECK_EQ(run(db, {"SET", "k", "v", "BOGUS"}), std::string("-ERR syntax error\r\n"));
}

TEST(counters) {
    Database db;
    CHECK_EQ(run(db, {"INCR", "n"}), std::string(":1\r\n"));
    CHECK_EQ(run(db, {"INCRBY", "n", "41"}), std::string(":42\r\n"));
    CHECK_EQ(run(db, {"DECRBY", "n", "2"}), std::string(":40\r\n"));
    run(db, {"SET", "s", "abc"});
    CHECK_EQ(run(db, {"INCR", "s"}), std::string("-ERR value is not an integer or out of range\r\n"));
    run(db, {"SET", "big", "9223372036854775807"});
    CHECK_EQ(run(db, {"INCR", "big"}), std::string("-ERR increment or decrement would overflow\r\n"));
}

TEST(wrong_type_is_reported) {
    Database db;
    run(db, {"RPUSH", "list", "a"});
    std::string wrongtype = "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";
    CHECK_EQ(run(db, {"GET", "list"}), wrongtype);
    CHECK_EQ(run(db, {"HSET", "list", "f", "v"}), wrongtype);
    CHECK_EQ(run(db, {"ZADD", "list", "1", "m"}), wrongtype);
    CHECK_EQ(run(db, {"TYPE", "list"}), std::string("+list\r\n"));
    run(db, {"SET", "list", "now a string"});  // SET overwrites any type
    CHECK_EQ(run(db, {"TYPE", "list"}), std::string("+string\r\n"));
}

TEST(list_commands) {
    Database db;
    CHECK_EQ(run(db, {"RPUSH", "l", "a", "b", "c"}), std::string(":3\r\n"));
    CHECK_EQ(run(db, {"LPUSH", "l", "z"}), std::string(":4\r\n"));  // z a b c
    CHECK_EQ(run(db, {"LRANGE", "l", "0", "-1"}),
             std::string("*4\r\n$1\r\nz\r\n$1\r\na\r\n$1\r\nb\r\n$1\r\nc\r\n"));
    CHECK_EQ(run(db, {"LRANGE", "l", "-2", "100"}), std::string("*2\r\n$1\r\nb\r\n$1\r\nc\r\n"));
    CHECK_EQ(run(db, {"LRANGE", "l", "5", "10"}), std::string("*0\r\n"));
    CHECK_EQ(run(db, {"LINDEX", "l", "-1"}), std::string("$1\r\nc\r\n"));
    CHECK_EQ(run(db, {"LSET", "l", "9", "x"}), std::string("-ERR index out of range\r\n"));
    CHECK_EQ(run(db, {"LPOP", "l"}), std::string("$1\r\nz\r\n"));
    CHECK_EQ(run(db, {"RPOP", "l"}), std::string("$1\r\nc\r\n"));
    run(db, {"RPUSH", "r", "x", "y", "x", "z", "x"});
    CHECK_EQ(run(db, {"LREM", "r", "-2", "x"}), std::string(":2\r\n"));  // from the tail
    CHECK_EQ(run(db, {"LRANGE", "r", "0", "-1"}), std::string("*3\r\n$1\r\nx\r\n$1\r\ny\r\n$1\r\nz\r\n"));
}

TEST(empty_containers_are_deleted) {
    Database db;
    run(db, {"RPUSH", "l", "only"});
    run(db, {"LPOP", "l"});
    CHECK_EQ(run(db, {"EXISTS", "l"}), std::string(":0\r\n"));
    run(db, {"HSET", "h", "f", "v"});
    run(db, {"HDEL", "h", "f"});
    CHECK_EQ(run(db, {"EXISTS", "h"}), std::string(":0\r\n"));
}

TEST(hash_commands) {
    Database db;
    CHECK_EQ(run(db, {"HSET", "h", "a", "1", "b", "2"}), std::string(":2\r\n"));
    CHECK_EQ(run(db, {"HSET", "h", "a", "9", "c", "3"}), std::string(":1\r\n"));  // only c is new
    CHECK_EQ(run(db, {"HGET", "h", "a"}), std::string("$1\r\n9\r\n"));
    CHECK_EQ(run(db, {"HLEN", "h"}), std::string(":3\r\n"));
    CHECK_EQ(run(db, {"HEXISTS", "h", "zz"}), std::string(":0\r\n"));
    CHECK_EQ(run(db, {"HINCRBY", "h", "c", "10"}), std::string(":13\r\n"));
    CHECK_EQ(run(db, {"HDEL", "h", "a", "b", "nope"}), std::string(":2\r\n"));
    CHECK_EQ(run(db, {"HGETALL", "h"}), std::string("*2\r\n$1\r\nc\r\n$2\r\n13\r\n"));
    CHECK_EQ(run(db, {"HMSET", "h", "x", "1"}), std::string("+OK\r\n"));
    CHECK_EQ(run(db, {"HSET", "h", "odd"}), std::string("-ERR wrong number of arguments for 'hset' command\r\n"));
}

TEST(sorted_set_commands) {
    Database db;
    CHECK_EQ(run(db, {"ZADD", "z", "2", "b", "1", "a", "3", "c"}), std::string(":3\r\n"));
    CHECK_EQ(run(db, {"ZADD", "z", "1.5", "c"}), std::string(":0\r\n"));  // update only
    CHECK_EQ(run(db, {"ZRANGE", "z", "0", "-1"}), std::string("*3\r\n$1\r\na\r\n$1\r\nc\r\n$1\r\nb\r\n"));
    CHECK_EQ(run(db, {"ZRANGE", "z", "0", "0", "WITHSCORES"}), std::string("*2\r\n$1\r\na\r\n$1\r\n1\r\n"));
    CHECK_EQ(run(db, {"ZSCORE", "z", "c"}), std::string("$3\r\n1.5\r\n"));
    CHECK_EQ(run(db, {"ZRANK", "z", "b"}), std::string(":2\r\n"));
    CHECK_EQ(run(db, {"ZRANK", "z", "nope"}), std::string("$-1\r\n"));
    CHECK_EQ(run(db, {"ZCARD", "z"}), std::string(":3\r\n"));
    CHECK_EQ(run(db, {"ZREM", "z", "a", "nope"}), std::string(":1\r\n"));
    CHECK_EQ(run(db, {"ZADD", "z", "abc", "m"}), std::string("-ERR value is not a valid float\r\n"));
    CHECK_EQ(run(db, {"ZCARD", "z"}), std::string(":2\r\n"));  // the bad ZADD changed nothing
}

TEST(expiry_commands) {
    Database db;
    db.set_clock(fake_clock);
    CHECK_EQ(run(db, {"TTL", "k"}), std::string(":-2\r\n"));
    run(db, {"SET", "k", "v"});
    CHECK_EQ(run(db, {"TTL", "k"}), std::string(":-1\r\n"));
    CHECK_EQ(run(db, {"EXPIRE", "k", "10"}), std::string(":1\r\n"));
    CHECK_EQ(run(db, {"TTL", "k"}), std::string(":10\r\n"));
    CHECK_EQ(run(db, {"PTTL", "k"}), std::string(":10000\r\n"));
    CHECK_EQ(run(db, {"PERSIST", "k"}), std::string(":1\r\n"));
    CHECK_EQ(run(db, {"TTL", "k"}), std::string(":-1\r\n"));

    run(db, {"SET", "s", "v", "PX", "500"});
    fake_now += 499;
    CHECK_EQ(run(db, {"GET", "s"}), std::string("$1\r\nv\r\n"));
    fake_now += 1;
    CHECK_EQ(run(db, {"GET", "s"}), std::string("$-1\r\n"));

    run(db, {"SET", "t", "v", "EX", "100"});
    run(db, {"SET", "t", "v2"});  // plain SET removes the TTL (old server bug)
    CHECK_EQ(run(db, {"TTL", "t"}), std::string(":-1\r\n"));
    CHECK_EQ(run(db, {"SET", "t", "v", "EX", "0"}), std::string("-ERR invalid expire time in 'set' command\r\n"));
}

TEST(key_commands) {
    Database db;
    run(db, {"MSET", "user:1", "a", "user:2", "b", "other", "c"});
    CHECK_EQ(run(db, {"DEL", "user:1", "missing"}), std::string(":1\r\n"));  // old server always said 0
    CHECK_EQ(run(db, {"EXISTS", "user:2", "other", "nope"}), std::string(":2\r\n"));
    CHECK_EQ(run(db, {"KEYS", "user:*"}), std::string("*1\r\n$6\r\nuser:2\r\n"));
    CHECK_EQ(run(db, {"RENAME", "other", "renamed"}), std::string("+OK\r\n"));
    CHECK_EQ(run(db, {"RENAME", "nope", "x"}), std::string("-ERR no such key\r\n"));
    CHECK_EQ(run(db, {"DBSIZE"}), std::string(":2\r\n"));
    CHECK_EQ(run(db, {"FLUSHALL"}), std::string("+OK\r\n"));
    CHECK_EQ(run(db, {"DBSIZE"}), std::string(":0\r\n"));
    CHECK_EQ(run(db, {"ECHO", "hi"}), std::string("$2\r\nhi\r\n"));
}
