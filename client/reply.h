#pragma once

#include <string>
#include <vector>

// One reply from the server, already decoded from RESP.
// Arrays contain other replies, so a Reply is a small tree.
struct Reply {
    enum class Type {
        Status,   // +OK
        Error,    // -ERR something
        Integer,  // :42
        Bulk,     // $5 hello
        Nil,      // $-1 or *-1
        Array     // *2 ...
    };

    Type type = Type::Nil;
    std::string text;            // Status, Error and Bulk
    long long integer = 0;       // Integer
    std::vector<Reply> elements; // Array
};
