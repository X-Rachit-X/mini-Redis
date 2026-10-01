// Tests for the incremental RESP parser.

#include "../server/resp_parser.h"
#include "test_framework.h"

namespace {

ParseStatus parse(const std::string& data, std::vector<std::string>& args, size_t& consumed) {
    std::string error;
    return parse_command(data.data(), data.size(), args, consumed, error);
}

}  // namespace

TEST(parses_a_complete_command) {
    std::vector<std::string> args;
    size_t consumed;
    std::string data = "*2\r\n$3\r\nGET\r\n$3\r\nfoo\r\n";
    CHECK(parse(data, args, consumed) == ParseStatus::Ok);
    CHECK_EQ(consumed, data.size());
    CHECK_EQ(args.size(), size_t(2));
    CHECK_EQ(args[0], std::string("GET"));
    CHECK_EQ(args[1], std::string("foo"));
}

TEST(every_partial_prefix_is_incomplete) {
    // TCP can split a command anywhere. Every cut must give Incomplete.
    std::string data = "*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n";
    for (size_t cut = 0; cut < data.size(); cut++) {
        std::vector<std::string> args;
        size_t consumed;
        CHECK(parse(data.substr(0, cut), args, consumed) == ParseStatus::Incomplete);
        CHECK_EQ(consumed, size_t(0));
    }
}

TEST(parses_pipelined_commands_one_at_a_time) {
    std::string data = "*1\r\n$4\r\nPING\r\n*2\r\n$4\r\nECHO\r\n$2\r\nhi\r\n";
    std::vector<std::string> args;
    size_t consumed;
    CHECK(parse(data, args, consumed) == ParseStatus::Ok);
    CHECK_EQ(args[0], std::string("PING"));
    std::string rest = data.substr(consumed);
    CHECK(parse(rest, args, consumed) == ParseStatus::Ok);
    CHECK_EQ(args[1], std::string("hi"));
    CHECK_EQ(consumed, rest.size());
}

TEST(values_are_binary_safe) {
    // The value contains \r\n; the length prefix tells us it's part of the data.
    std::string data = "*2\r\n$4\r\nECHO\r\n$4\r\na\r\nb\r\n";
    std::vector<std::string> args;
    size_t consumed;
    CHECK(parse(data, args, consumed) == ParseStatus::Ok);
    CHECK_EQ(args[1], std::string("a\r\nb"));
}

TEST(parses_inline_commands) {
    std::vector<std::string> args;
    size_t consumed;
    CHECK(parse("SET  key   value\r\n", args, consumed) == ParseStatus::Ok);
    CHECK_EQ(args.size(), size_t(3));
    CHECK_EQ(args[2], std::string("value"));
    CHECK(parse("PING\n", args, consumed) == ParseStatus::Ok);  // bare \n is fine too
    CHECK_EQ(args[0], std::string("PING"));
    CHECK(parse("PIN", args, consumed) == ParseStatus::Incomplete);
}

TEST(rejects_malformed_input) {
    std::vector<std::string> args;
    size_t consumed;
    CHECK(parse("*1\r\n+PING\r\n", args, consumed) == ParseStatus::Error);      // not '$'
    CHECK(parse("*x\r\n", args, consumed) == ParseStatus::Error);               // bad count
    CHECK(parse("*1\r\n$-5\r\n", args, consumed) == ParseStatus::Error);        // negative len
    CHECK(parse("*1\r\n$4\r\nPINGxx", args, consumed) == ParseStatus::Error);   // no CRLF after data
    CHECK(parse("*99999999999\r\n", args, consumed) == ParseStatus::Error);     // too many args
}

TEST(empty_array_is_skipped) {
    std::vector<std::string> args;
    size_t consumed;
    CHECK(parse("*0\r\n", args, consumed) == ParseStatus::Ok);
    CHECK(args.empty());
    CHECK_EQ(consumed, size_t(4));
}
