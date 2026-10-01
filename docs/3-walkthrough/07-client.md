# 07: The client (`client/`)

The client is deliberately simple: **blocking** sockets, one command at a time. It's a separate program and shares no code with the server, just like real `redis-cli`.

---

## `tcp_connection.h` / `.cpp`

```cpp
class TcpConnection {
public:
    TcpConnection() = default;
    ~TcpConnection();
    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;
```
It owns a socket fd, and the destructor closes it (RAII). Copies are disabled so the fd can't be closed twice.

```cpp
bool TcpConnection::connect_to(const std::string& host, int port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* results = nullptr;
    int err = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &results);
```
`getaddrinfo` turns a name (`localhost`, `example.com`) or an IP string into socket addresses. `AF_UNSPEC` means "IPv4 or IPv6, whatever exists". It allocates a **linked list** of results that we must free.

```cpp
    if (err != 0) {
        std::cerr << "Could not resolve " << host << ": " << gai_strerror(err) << "\n";
        return false;
    }
```
`getaddrinfo` has its own error codes, so `gai_strerror` turns them into text (not `perror`).

```cpp
    for (addrinfo* addr = results; addr != nullptr; addr = addr->ai_next) {
        fd_ = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (fd_ < 0) continue;
        if (connect(fd_, addr->ai_addr, addr->ai_addrlen) == 0) break;
        ::close(fd_);
        fd_ = -1;
    }
    freeaddrinfo(results);
```
Try each address until one connects. `localhost` often resolves to both `::1` (IPv6) and `127.0.0.1`, and our server only listens on IPv4, so falling through to the next address matters. The list is always freed.

```cpp
bool TcpConnection::send_all(const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = send(fd_, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}
```
`send()` may send only part of a large buffer, so loop. Your old client did one `send()` and treated a partial send as failure.

---

## `resp_encoder.cpp`

```cpp
std::string encode_command(const std::vector<std::string>& args) {
    std::string out = "*" + std::to_string(args.size()) + "\r\n";
    for (const std::string& arg : args) {
        out += "$" + std::to_string(arg.size()) + "\r\n";
        out += arg;
        out += "\r\n";
    }
    return out;
}
```
Array header, then each argument as a length-prefixed bulk string. Because of the length prefix, a value with spaces or newlines arrives intact.

---

## `reply.h`

```cpp
struct Reply {
    enum class Type { Status, Error, Integer, Bulk, Nil, Array };
    Type type = Type::Nil;
    std::string text;
    long long integer = 0;
    std::vector<Reply> elements;
};
```
One struct for every RESP reply. Only the fields that match `type` are used. `elements` holds child replies, so arrays inside arrays form a **tree**. A struct can contain a `std::vector` of itself, because the vector stores the elements on the heap.

---

## `reply_reader.h` / `.cpp`

```cpp
class ReplyReader {
    ...
    int fd_;
    std::string buffer_;
    size_t pos_ = 0;
};
```
`buffer_` holds bytes received from the socket. `pos_` marks how far we've parsed. Everything before `pos_` is finished.

```cpp
bool ReplyReader::fill_buffer() {
    if (pos_ > 0) {
        buffer_.erase(0, pos_);
        pos_ = 0;
    }
    char chunk[16 * 1024];
    while (true) {
        ssize_t n = recv(fd_, chunk, sizeof(chunk), 0);
        if (n > 0) { buffer_.append(chunk, static_cast<size_t>(n)); return true; }
        if (n < 0 && errno == EINTR) continue;
        return false;
    }
}
```
Called only when we need more data. It first throws away already-parsed bytes so the buffer doesn't grow forever, then does **one** `recv()` of up to 16 KB. `recv()` blocks until at least one byte arrives. It returns 0 if the server closed the connection.

```cpp
bool ReplyReader::read_line(std::string& line) {
    while (true) {
        size_t end = buffer_.find("\r\n", pos_);
        if (end != std::string::npos) {
            line = buffer_.substr(pos_, end - pos_);
            pos_ = end + 2;
            return true;
        }
        if (!fill_buffer()) return false;
    }
}
```
Look for a complete line in what we have. If there isn't one, read more and look again.

```cpp
bool ReplyReader::read_exact(size_t count, std::string& data) {
    while (buffer_.size() - pos_ < count) {
        if (!fill_buffer()) return false;
    }
    data = buffer_.substr(pos_, count);
    pos_ += count;
    return true;
}
```
For bulk strings: wait until `count` bytes are available, then take them. This is binary safe because we never search inside the value.

```cpp
bool ReplyReader::read_reply(Reply& reply) {
    std::string line;
    if (!read_line(line) || line.empty()) return false;
    char type = line[0];
    std::string rest = line.substr(1);
    reply = Reply{};
```
Every reply starts with a line whose first character is its type. `reply = Reply{}` resets it, in case it's being reused.

```cpp
    switch (type) {
        case '+': reply.type = Reply::Type::Status; reply.text = rest; return true;
        case '-': reply.type = Reply::Type::Error;  reply.text = rest; return true;
        case ':': reply.type = Reply::Type::Integer; return to_number(rest, reply.integer);
```
One-line types: the line *is* the value.

```cpp
        case '$': {
            long long length;
            if (!to_number(rest, length)) return false;
            if (length < 0) { reply.type = Reply::Type::Nil; return true; }
            reply.type = Reply::Type::Bulk;
            std::string crlf;
            return read_exact(static_cast<size_t>(length), reply.text) && read_exact(2, crlf);
        }
```
Bulk: `$-1` is nil. Otherwise read exactly `length` bytes, then the trailing `\r\n`. The braces `{ }` give the case its own scope so it can declare local variables.

```cpp
        case '*': {
            long long count;
            if (!to_number(rest, count)) return false;
            if (count < 0) { reply.type = Reply::Type::Nil; return true; }
            reply.type = Reply::Type::Array;
            reply.elements.resize(static_cast<size_t>(count));
            for (Reply& element : reply.elements) {
                if (!read_reply(element)) return false;
            }
            return true;
        }
        default: return false;
    }
```
Arrays: read `count` replies **recursively**. Each element can itself be an array. This mirrors RESP's recursive definition.

---

## `reply_printer.cpp`

```cpp
std::string quote(const std::string& text) {
    std::string out = "\"";
    for (unsigned char c : text) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (std::isprint(c)) out += static_cast<char>(c);
                else { char hex[8]; snprintf(hex, sizeof(hex), "\\x%02x", c); out += hex; }
        }
    }
    return out + "\"";
}
```
Show bulk strings the way redis-cli does: in quotes, with invisible characters made visible. `"a\nb"` prints as `"a\nb"` instead of breaking the line. Non-printable bytes become `\xHH`. `unsigned char` is used because `isprint` with a negative value is undefined.

```cpp
std::string format_reply(const Reply& reply, const std::string& indent) {
    switch (reply.type) {
        case Reply::Type::Status:  return reply.text;
        case Reply::Type::Error:   return "(error) " + reply.text;
        case Reply::Type::Integer: return "(integer) " + std::to_string(reply.integer);
        case Reply::Type::Bulk:    return quote(reply.text);
        case Reply::Type::Nil:     return "(nil)";
        case Reply::Type::Array:   break;
    }
```
Simple types map directly to redis-cli's style.

```cpp
    if (reply.elements.empty()) return "(empty array)";
    size_t width = std::to_string(reply.elements.size()).size();
    std::string out;
    for (size_t i = 0; i < reply.elements.size(); i++) {
        std::string number = std::to_string(i + 1);
        std::string prefix = std::string(width - number.size(), ' ') + number + ") ";
        if (i > 0) out += "\n" + indent;
        out += prefix + format_reply(reply.elements[i], indent + std::string(prefix.size(), ' '));
    }
    return out;
```
- `width` = digits in the largest index, so ` 9)` and `10)` line up.
- Each line after the first starts with the current `indent`.
- **Recursion with a growing indent**: a nested array's lines start under the first character of its first item:
```
1) 1) "a"
   2) "b"
2) "c"
```

---

## `arg_splitter.cpp`

```cpp
bool split_args(const std::string& line, std::vector<std::string>& args) {
    args.clear();
    size_t i = 0;
    const size_t n = line.size();
    while (true) {
        while (i < n && is_space(line[i])) i++;
        if (i >= n) return true;
```
The outer loop finds arguments: skip spaces, and stop at the end of the line.

```cpp
        std::string current;
        bool in_double_quotes = false;
        bool in_single_quotes = false;
        while (i < n) {
            char c = line[i];
```
The inner loop reads one argument character by character. Two flags track whether we're inside quotes. It's a tiny **state machine**.

```cpp
            if (in_double_quotes) {
                if (c == '\\' && i + 1 < n) {
                    current += unescape(line[i + 1]);
                    i += 2;
                    continue;
                }
                if (c == '"') in_double_quotes = false;
                else current += c;
```
Inside `"..."`: a backslash escape takes two characters (`\n` becomes a newline), and a `"` closes the quotes. Everything else, **including spaces**, is part of the argument.

```cpp
            } else if (in_single_quotes) {
                if (c == '\'') in_single_quotes = false;
                else current += c;
```
Inside `'...'`: everything is literal (no escapes), like in a shell.

```cpp
            } else {
                if (is_space(c)) break;
                if (c == '"') in_double_quotes = true;
                else if (c == '\'') in_single_quotes = true;
                else current += c;
            }
            i++;
        }
```
Outside quotes: a space ends the argument, and a quote opens a quoted section. Note `ab"c d"e` becomes the single argument `abc de`, like a shell.

```cpp
        if (in_double_quotes || in_single_quotes) return false;
        args.push_back(current);
    }
}
```
An unclosed quote is reported as an error. `SET k ""` produces an **empty** argument, which your old regex-based splitter couldn't do.

---

## `main.cpp`

```cpp
for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (command.empty() && arg == "-h" && i + 1 < argc) host = argv[++i];
    else if (command.empty() && arg == "-p" && i + 1 < argc) port = std::atoi(argv[++i]);
    else if (command.empty() && arg == "--help") { print_usage(); return 0; }
    else command.push_back(arg);
}
```
Options are recognized only **before** the command starts (`command.empty()`). So `mini-redis-cli SET flag -p` stores the value `-p` instead of treating it as an option.

```cpp
bool run_command(TcpConnection& conn, ReplyReader& reader,
                 const std::vector<std::string>& args, bool& was_error) {
    if (!conn.send_all(encode_command(args))) return false;
    Reply reply;
    if (!reader.read_reply(reply)) return false;
    std::cout << format_reply(reply) << std::endl;
    was_error = reply.type == Reply::Type::Error;
    return true;
}
```
One request/response round trip. It returns false only if the **connection** broke. An error *reply* is still a successful round trip, and is reported through `was_error`.

```cpp
if (!command.empty()) {
    bool was_error = false;
    if (!run_command(conn, reader, command, was_error)) { ...; return 1; }
    return was_error ? 1 : 0;
}
```
One-shot mode: exit code 1 on an error reply, so shell scripts can do `if mini-redis-cli SET ...; then`.

```cpp
bool interactive = isatty(STDIN_FILENO);
...
while (true) {
    if (interactive) std::cout << prompt << std::flush;
    if (!std::getline(std::cin, line)) break;
```
`isatty` checks whether stdin is a real terminal. If commands are piped in (`printf 'PING\n' | mini-redis-cli`), no prompts are printed, so the output stays clean (the e2e test relies on this). `getline` fails on Ctrl+D or end of input, which ends the loop.

```cpp
    if (!split_args(line, args)) { std::cout << "(error) Invalid argument(s): unclosed quote" << std::endl; continue; }
    if (args.empty()) continue;
    std::string first = lowercase(args[0]);
    if (first == "quit" || first == "exit") break;
    if (first == "help") { print_repl_help(); continue; }
```
Parse the line, ignore blank lines, and handle client-only commands locally (they are never sent to the server).
