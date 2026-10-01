# 01: Protocol: `resp_parser` and `resp_writer`

These two files are the only place that knows the RESP byte format on the server side.
The parser turns bytes into `std::vector<std::string>`, and the writer turns replies into bytes.

---

## `server/resp_parser.h`

```cpp
#pragma once
```
Tells the compiler to include this header only once per `.cpp`, even if it's `#include`d several times. Same job as the `#ifndef X / #define X / #endif` guards in your old code, but shorter.

```cpp
#include <cstddef>   // size_t
#include <string>
#include <vector>
```
Only what the declarations below need.

```cpp
enum class ParseStatus {
    Ok,
    Incomplete,
    Error
};
```
The three possible outcomes. `enum class` (not plain `enum`) means you must write `ParseStatus::Ok`, and the values don't silently convert to `int`.
- **Ok**: a full command was found.
- **Incomplete**: the bytes so far are valid but the command isn't finished, so wait for more data. This is what makes the server work over a TCP *byte stream*.
- **Error**: the bytes can never become a valid command, so the connection should be closed.

```cpp
ParseStatus parse_command(const char* data, size_t len,
                          std::vector<std::string>& args,
                          size_t& consumed,
                          std::string& error);
```
- `data, len`: a pointer to raw bytes and how many there are. We use a pointer and length rather than `std::string` so the caller can point *into the middle* of its buffer (`input.data() + pos`) without copying.
- `args` (output): the parsed command, e.g. `{"SET","k","v"}`.
- `consumed` (output): how many bytes belonged to this command. The caller advances by this much to reach the next pipelined command.
- `error` (output): a message when the status is Error.

Outputs are passed by reference, so the function can fill them and still return a status.

---

## `server/resp_parser.cpp`

```cpp
namespace {
```
An **anonymous namespace**: everything inside is visible only in this `.cpp` file. These helpers are implementation details, and this also avoids name clashes with other files (`is_space` exists in the client too).

```cpp
const long long MAX_ARGS = 1024 * 1024;
const long long MAX_BULK_LENGTH = 512LL * 1024 * 1024;
const size_t MAX_LINE_LENGTH = 64 * 1024;
```
Safety limits. Without them a client could send `*999999999999\r\n` and we would try to reserve memory for that, or send a "line" that never ends and fill our RAM. `512LL`: the `LL` makes the multiplication happen in `long long`. In `int`, 512×1024×1024 would overflow. These match real Redis's limits.

```cpp
const size_t NOT_FOUND = std::string::npos;
```
`npos` is the largest `size_t` value, which the standard library uses for "not found". The alias just reads better.

```cpp
size_t find_crlf(const char* data, size_t len, size_t from) {
    for (size_t i = from; i + 1 < len; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n') return i;
    }
    return NOT_FOUND;
}
```
Scans for the two-byte sequence `\r\n` starting at `from`. `i + 1 < len` makes sure `data[i + 1]` is still inside the buffer. Returns the index of the `\r`.

```cpp
bool parse_number(const char* data, size_t start, size_t end, long long& out) {
    if (start >= end) return false;
```
Parses the digits between `start` and `end` (for example the `3` in `$3\r\n`). An empty number (`$\r\n`) is invalid.

```cpp
    bool negative = false;
    if (data[start] == '-') {
        negative = true;
        start++;
        if (start == end) return false;
    }
```
RESP allows `-1` (for null), so we accept a leading minus. A lone `-` is invalid.

```cpp
    long long value = 0;
    for (size_t i = start; i < end; i++) {
        if (data[i] < '0' || data[i] > '9') return false;
        value = value * 10 + (data[i] - '0');
        if (value > MAX_BULK_LENGTH) return false;
    }
```
The classic "digits to number" loop: shift the old value one decimal place left, then add the new digit. `data[i] - '0'` turns the character `'7'` into the number 7. Stopping as soon as the value exceeds our limit also means the `long long` can **never overflow**, however many digits a malicious client sends.

```cpp
    out = negative ? -value : value;
    return true;
}
```

```cpp
bool is_space(char c) { return c == ' ' || c == '\t'; }
```

### Inline commands

```cpp
ParseStatus parse_inline(const char* data, size_t len, std::vector<std::string>& args,
                         size_t& consumed, std::string& error) {
    size_t newline = NOT_FOUND;
    for (size_t i = 0; i < len; i++) {
        if (data[i] == '\n') { newline = i; break; }
    }
```
An inline command is one text line, so first find the `\n` that ends it.

```cpp
    if (newline == NOT_FOUND) {
        if (len > MAX_LINE_LENGTH) {
            error = "too big inline request";
            return ParseStatus::Error;
        }
        return ParseStatus::Incomplete;
    }
```
No newline yet means the line isn't finished, so return Incomplete, unless the "line" is already over 64 KB. Then the client is broken or hostile, so return Error.

```cpp
    size_t end = newline;
    if (end > 0 && data[end - 1] == '\r') end--;
```
Telnet sends `\r\n` and `nc` may send just `\n`. Accept both by ignoring an optional `\r`.

```cpp
    size_t i = 0;
    while (i < end) {
        while (i < end && is_space(data[i])) i++;
        size_t word_start = i;
        while (i < end && !is_space(data[i])) i++;
        if (i > word_start) args.emplace_back(data + word_start, i - word_start);
    }
```
Split on spaces: skip spaces, remember where a word starts, walk to its end, and store it. `emplace_back(pointer, length)` constructs the `std::string` directly inside the vector, so no temporary is created. The `if` skips empty "words" (e.g. trailing spaces).

```cpp
    consumed = newline + 1;
    return ParseStatus::Ok;
}
```
We used everything up to and including the `\n`.

### RESP arrays

```cpp
ParseStatus parse_array(...) {
    size_t crlf = find_crlf(data, len, 1);
```
`data[0]` is `*` (the caller checked), so search for the end of the header line from index 1.

```cpp
    if (crlf == NOT_FOUND) {
        if (len > MAX_LINE_LENGTH) { error = "too big multibulk header"; return ParseStatus::Error; }
        return ParseStatus::Incomplete;
    }
```
The header itself isn't complete yet.

```cpp
    long long count;
    if (!parse_number(data, 1, crlf, count) || count > MAX_ARGS) {
        error = "invalid multibulk length";
        return ParseStatus::Error;
    }
    size_t pos = crlf + 2;
```
Read the element count and move `pos` past `\r\n`. From here on, `pos` is our read cursor.

```cpp
    if (count <= 0) {
        consumed = pos;
        return ParseStatus::Ok;
    }
```
`*0\r\n` is an empty command. We consume it and return Ok with empty `args`, and the server simply skips it.

```cpp
    for (long long i = 0; i < count; i++) {
        if (pos >= len) return ParseStatus::Incomplete;
```
For each element: if we've run out of bytes, the rest hasn't arrived yet.

```cpp
        if (data[pos] != '$') {
            error = "expected '$', got '" + std::string(1, data[pos]) + "'";
            return ParseStatus::Error;
        }
```
Client commands contain only bulk strings, so anything else is a protocol error. `std::string(1, c)` builds a 1-character string.

```cpp
        crlf = find_crlf(data, len, pos + 1);
        if (crlf == NOT_FOUND) {
            if (len - pos > MAX_LINE_LENGTH) { error = "too big bulk header"; return ParseStatus::Error; }
            return ParseStatus::Incomplete;
        }
        long long bulk_len;
        if (!parse_number(data, pos + 1, crlf, bulk_len) || bulk_len < 0) {
            error = "invalid bulk length";
            return ParseStatus::Error;
        }
        pos = crlf + 2;
```
Read the `$<len>\r\n` header. A negative length (`$-1`, null) makes sense in replies but not inside a command.

```cpp
        if (pos + bulk_len + 2 > len) return ParseStatus::Incomplete;
```
**The most important line for performance and correctness.** We don't search through the value. We *calculate* where it ends. If the value plus its `\r\n` isn't fully in the buffer yet, we wait. A 100 MB value arriving in 1000 pieces costs only this one comparison per piece, with no rescanning.

```cpp
        if (data[pos + bulk_len] != '\r' || data[pos + bulk_len + 1] != '\n') {
            error = "bulk string not terminated by CRLF";
            return ParseStatus::Error;
        }
```
Right after the value there must be `\r\n`. If not, the length was wrong and the stream is corrupt.

```cpp
        args.emplace_back(data + pos, bulk_len);
        pos += bulk_len + 2;
    }
    consumed = pos;
    return ParseStatus::Ok;
}
```
Copy the value into `args`, jump past it and its `\r\n`, and report how far we got.

### Entry point

```cpp
ParseStatus parse_command(...) {
    args.clear();
    consumed = 0;
    if (len == 0) return ParseStatus::Incomplete;
```
Start clean. An empty buffer simply means "nothing yet".

```cpp
    ParseStatus status = (data[0] == '*')
        ? parse_array(data, len, args, consumed, error)
        : parse_inline(data, len, args, consumed, error);
```
The first byte decides the format. This is how real Redis distinguishes them too.

```cpp
    if (status != ParseStatus::Ok) {
        args.clear();
        consumed = 0;
    }
    return status;
}
```
`parse_array` may have pushed some arguments before discovering the command is incomplete. We clear them so callers never see half a command. Next time, parsing starts again from the beginning of the command. That's the simple "re-parse" approach discussed in the architecture doc.

---

## `server/resp_writer.h` / `.cpp`

```cpp
void reply_simple(std::string& out, const std::string& text);
...
```
Each function **appends** to `out` rather than returning a new string. Many replies get appended to the same connection output buffer, so there are no extra copies, and a pipeline of 1000 replies becomes one buffer and one `send()`.

```cpp
void append_single_line(std::string& out, const std::string& text) {
    for (char c : text) out += (c == '\r' || c == '\n') ? ' ' : c;
    out += "\r\n";
}
```
Simple strings and errors end at the first `\r\n`. If an error message contained one (for example `unknown command 'a\r\nb'` echoing the user's input), the client would get out of sync. So we replace CR/LF with spaces. That's a small but real protocol-safety detail.

```cpp
void reply_simple(std::string& out, const std::string& text) {
    out += '+';
    append_single_line(out, text);
}
void reply_error(...)   { out += '-'; append_single_line(out, message); }
void reply_integer(...) { out += ':'; out += std::to_string(value); out += "\r\n"; }
```
The type character, then the content, then CRLF.

```cpp
void reply_bulk(std::string& out, const std::string& data) {
    out += '$';
    out += std::to_string(data.size());
    out += "\r\n";
    out += data;
    out += "\r\n";
}
```
Length first, then the raw bytes, which may be anything (binary safe).

```cpp
void reply_null(std::string& out) { out += "$-1\r\n"; }
void reply_array_header(std::string& out, size_t count) { ... "*" count "\r\n" }
```
Null is a bulk string with length -1. An array is just a header. The caller then appends `count` more replies.

```cpp
std::string encode_command(const std::vector<std::string>& args) {
    std::string out;
    reply_array_header(out, args.size());
    for (const std::string& arg : args) reply_bulk(out, arg);
    return out;
}
```
A command is an array of bulk strings, so it's built from the same functions. The AOF uses this.

```cpp
std::string format_double(double value) {
    if (std::isinf(value)) return value > 0 ? "inf" : "-inf";
    char buffer[64];
    std::to_chars_result result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return std::string(buffer, result.ptr);
}
```
Sorted-set scores are doubles. `std::to_chars` (C++17) produces the **shortest** text that converts back to exactly the same double: `1.5` → `"1.5"`, while `printf("%.17g")` would print `1.5000000000000000`, and `0.1` would become `0.10000000000000001`. This matters for the AOF: the score written must read back identically. `result.ptr` points just after the last character written. Infinity is handled first because Redis spells it `inf`.
