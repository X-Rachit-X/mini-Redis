// Tests for AOF persistence: log -> replay must rebuild identical data.

#include <cstdio>
#include <fstream>
#include <iterator>
#include <unistd.h>

#include "../server/aof.h"
#include "../server/commands.h"
#include "../server/database.h"
#include "test_framework.h"

namespace {

std::string temp_path(const std::string& name) {
    return "/tmp/mini-redis-test-" + std::to_string(getpid()) + "-" + name + ".aof";
}

std::string read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

std::string run(Database& db, Aof* aof, const Args& args) {
    std::string out;
    execute_command(db, aof, args, out);
    return out;
}

}  // namespace

TEST(aof_replay_restores_all_types) {
    std::string path = temp_path("replay");
    unlink(path.c_str());
    {
        Database db;
        Aof aof(path, FsyncPolicy::No);
        CHECK(aof.open());
        run(db, &aof, {"SET", "s", "hello world"});
        run(db, &aof, {"RPUSH", "l", "a", "b"});
        run(db, &aof, {"HSET", "h", "f", "v"});
        run(db, &aof, {"ZADD", "z", "1.5", "m"});
        run(db, &aof, {"INCR", "n"});
        run(db, &aof, {"GET", "s"});          // read-only: not logged
        run(db, &aof, {"DEL", "missing"});    // changed nothing: not logged
        aof.flush();
    }
    Database restored;
    long loaded = 0;
    CHECK(load_aof(path, restored, loaded));
    CHECK_EQ(loaded, 5L);
    CHECK_EQ(run(restored, nullptr, {"GET", "s"}), std::string("$11\r\nhello world\r\n"));
    CHECK_EQ(run(restored, nullptr, {"LRANGE", "l", "0", "-1"}), std::string("*2\r\n$1\r\na\r\n$1\r\nb\r\n"));
    CHECK_EQ(run(restored, nullptr, {"HGET", "h", "f"}), std::string("$1\r\nv\r\n"));
    CHECK_EQ(run(restored, nullptr, {"ZSCORE", "z", "m"}), std::string("$3\r\n1.5\r\n"));
    CHECK_EQ(run(restored, nullptr, {"GET", "n"}), std::string("$1\r\n1\r\n"));
    unlink(path.c_str());
}

TEST(aof_logs_absolute_expiry_times) {
    std::string path = temp_path("expiry");
    unlink(path.c_str());
    {
        Database db;
        Aof aof(path, FsyncPolicy::No);
        CHECK(aof.open());
        run(db, &aof, {"SET", "k", "v", "EX", "100"});
        aof.flush();
    }
    std::string content = read_file(path);
    CHECK(content.find("PEXPIREAT") != std::string::npos);
    CHECK(content.find("EX\r\n") == std::string::npos);  // relative form is not stored

    Database restored;
    long loaded = 0;
    CHECK(load_aof(path, restored, loaded));
    std::string ttl = run(restored, nullptr, {"TTL", "k"});
    CHECK(ttl == ":100\r\n" || ttl == ":99\r\n");
    unlink(path.c_str());
}

TEST(aof_truncated_tail_is_repaired) {
    std::string path = temp_path("truncated");
    std::string good = "*3\r\n$3\r\nSET\r\n$1\r\na\r\n$1\r\n1\r\n";
    {
        std::ofstream file(path, std::ios::binary);
        file << good << "*3\r\n$3\r\nSET\r\n$1\r\nb";  // crash in the middle of a write
    }
    Database db;
    long loaded = 0;
    CHECK(load_aof(path, db, loaded));
    CHECK_EQ(loaded, 1L);
    CHECK_EQ(read_file(path), good);  // the partial command was cut off
    unlink(path.c_str());
}

TEST(aof_corrupt_file_is_rejected) {
    std::string path = temp_path("corrupt");
    {
        std::ofstream file(path, std::ios::binary);
        file << "*1\r\n$4\r\nPING\r\n*1\r\n#garbage\r\n";
    }
    Database db;
    long loaded = 0;
    CHECK(!load_aof(path, db, loaded));
    unlink(path.c_str());
}

TEST(aof_rewrite_compacts_the_file) {
    std::string path = temp_path("rewrite");
    unlink(path.c_str());
    Database db;
    Aof aof(path, FsyncPolicy::No);
    CHECK(aof.open());
    for (int i = 0; i < 200; i++) run(db, &aof, {"INCR", "counter"});
    run(db, &aof, {"RPUSH", "l", "x", "y"});
    run(db, &aof, {"SET", "temp", "v", "EX", "1000"});
    aof.flush();
    size_t size_before = read_file(path).size();

    CHECK_EQ(run(db, nullptr, {"REWRITEAOF"}), std::string("-ERR AOF is disabled\r\n"));
    CHECK_EQ(run(db, &aof, {"REWRITEAOF"}), std::string("+OK\r\n"));
    size_t size_after = read_file(path).size();
    CHECK(size_after < size_before / 10);

    run(db, &aof, {"SET", "after", "rewrite"});  // the new file keeps receiving writes
    aof.flush();

    Database restored;
    long loaded = 0;
    CHECK(load_aof(path, restored, loaded));
    CHECK_EQ(run(restored, nullptr, {"GET", "counter"}), std::string("$3\r\n200\r\n"));
    CHECK_EQ(run(restored, nullptr, {"LLEN", "l"}), std::string(":2\r\n"));
    CHECK_EQ(run(restored, nullptr, {"GET", "after"}), std::string("$7\r\nrewrite\r\n"));
    CHECK(run(restored, nullptr, {"TTL", "temp"}) != std::string(":-1\r\n"));  // TTL survived
    unlink(path.c_str());
}
