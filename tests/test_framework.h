#pragma once

// A tiny unit-test framework (no external libraries needed).
//
//   TEST(name) { ... }       defines a test that registers itself
//   CHECK(condition)         fails the test if the condition is false
//   CHECK_EQ(actual, expected)  fails and prints both values if they differ

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

struct TestCase {
    const char* name;
    void (*function)();
};

inline std::vector<TestCase>& all_tests() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failure_count() {
    static int failures = 0;
    return failures;
}

// A global object of this type is created for each TEST. Its constructor
// runs before main(), which is how tests register themselves.
struct TestRegistrar {
    TestRegistrar(const char* name, void (*function)()) { all_tests().push_back({name, function}); }
};

// Shows \r and \n visibly, so RESP strings are readable in failure messages.
inline std::string printable(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '\r') out += "\\r";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return "\"" + out + "\"";
}
inline std::string printable(const char* text) { return printable(std::string(text)); }
template <typename T>
std::string printable(const T& value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

#define TEST(name)                                         \
    static void name();                                    \
    static TestRegistrar registrar_##name(#name, name);    \
    static void name()

#define CHECK(condition)                                                              \
    do {                                                                              \
        if (!(condition)) {                                                           \
            std::cerr << "  " << __FILE__ << ":" << __LINE__ << ": CHECK(" #condition \
                      << ") failed\n";                                                \
            failure_count()++;                                                        \
        }                                                                             \
    } while (0)

#define CHECK_EQ(actual, expected)                                                    \
    do {                                                                              \
        auto actual_value = (actual);                                                 \
        auto expected_value = (expected);                                             \
        if (!(actual_value == expected_value)) {                                      \
            std::cerr << "  " << __FILE__ << ":" << __LINE__ << ": " #actual "\n"      \
                      << "    expected: " << printable(expected_value) << "\n"        \
                      << "    actual:   " << printable(actual_value) << "\n";         \
            failure_count()++;                                                        \
        }                                                                             \
    } while (0)
