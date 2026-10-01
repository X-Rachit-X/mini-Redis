// Tests for the keyspace and expiry, using a fake clock so we can
// "fast-forward time" instead of sleeping.

#include "../server/database.h"
#include "../server/glob.h"
#include "test_framework.h"

namespace {

int64_t fake_now = 1000000;
int64_t fake_clock() { return fake_now; }

}  // namespace

TEST(lazy_expiry_hides_and_deletes_keys) {
    Database db;
    db.set_clock(fake_clock);
    db.create("k").value = std::string("v");
    CHECK(db.set_expire("k", fake_now + 100));
    CHECK(db.find("k") != nullptr);
    fake_now += 100;  // now == expire time -> expired
    CHECK(db.find("k") == nullptr);
    CHECK_EQ(db.size(), size_t(0));  // find() really deleted it
}

TEST(active_expiry_removes_untouched_keys) {
    Database db;
    db.set_clock(fake_clock);
    for (int i = 0; i < 10; i++) {
        std::string key = "k" + std::to_string(i);
        db.create(key);
        db.set_expire(key, fake_now + (i < 6 ? 10 : 1000));  // 6 short, 4 long TTLs
    }
    fake_now += 50;
    CHECK_EQ(db.remove_expired(3), 3);  // respects the limit
    CHECK_EQ(db.remove_expired(100), 3);
    CHECK_EQ(db.remove_expired(100), 0);
    CHECK_EQ(db.size(), size_t(4));
}

TEST(create_clears_old_ttl) {
    Database db;
    db.set_clock(fake_clock);
    db.create("k");
    db.set_expire("k", fake_now + 10);
    db.create("k");  // what SET does
    fake_now += 100;
    CHECK(db.find("k") != nullptr);
    CHECK_EQ(db.remove_expired(100), 0);  // index entry was removed too
}

TEST(rename_moves_value_and_ttl) {
    Database db;
    db.set_clock(fake_clock);
    db.create("old").value = std::string("hello");
    db.set_expire("old", fake_now + 500);
    db.create("new").value = std::string("overwritten");
    CHECK(db.rename("old", "new"));
    CHECK(db.find("old") == nullptr);
    Entry* entry = db.find("new");
    CHECK(entry != nullptr);
    CHECK_EQ(std::get<std::string>(entry->value), std::string("hello"));
    CHECK_EQ(entry->expire_at, fake_now + 500);
    fake_now += 1000;
    CHECK_EQ(db.remove_expired(100), 1);  // expiry index followed the rename
    CHECK(!db.rename("missing", "x"));
}

TEST(expire_in_the_past_deletes_immediately) {
    Database db;
    db.set_clock(fake_clock);
    db.create("k");
    CHECK(db.set_expire("k", fake_now - 1));
    CHECK(db.find("k") == nullptr);
    CHECK(!db.set_expire("k", fake_now + 10));  // key no longer exists
}

TEST(glob_patterns) {
    CHECK(glob_match("*", "anything"));
    CHECK(glob_match("user:*", "user:42"));
    CHECK(!glob_match("user:*", "session:1"));
    CHECK(glob_match("h?llo", "hello"));
    CHECK(!glob_match("h?llo", "hllo"));
    CHECK(glob_match("*a*b*", "xxaxxbxx"));
    CHECK(!glob_match("*a*b", "xxaxxbxx"));
    CHECK(glob_match("", ""));
}

// Every key holds an Entry, and a std::variant is as big as its largest
// alternative. A member that quietly grows one type (an std::mt19937 inside
// SkipList once made EVERY key ~5 KB) must fail here, not in production.
TEST(entries_stay_small) {
    CHECK(sizeof(Value) <= 256);
    CHECK(sizeof(Entry) <= 256);
}
