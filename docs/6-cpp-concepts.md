# Layer 6: Every C++ concept in this project, in plain language

This file lists **every C++ (and C/POSIX) feature the code actually uses**. For each one you get:

- **In plain words**: what it is, with no jargon.
- **In this project**: the exact place it appears (`file:line`).
- **Why here**: why it was the right tool for that spot.
- **Watch out**: the classic mistake, and how the code avoids it.

You don't need to read it top to bottom. Use it as a dictionary when the code shows you something unfamiliar.
The concepts go from basic to advanced, so you can also read it as a C++ course built around one real program.

> Tip: to see a concept in action, open the file at the line given, change something small, and run `make test`.
> Breaking things on purpose is the fastest way to understand why they are written the way they are.

### Contents

**Part A: How a C++ program is built**
[Headers and source files](#headers-and-source-files) ·
[`#pragma once`](#pragma-once) ·
[Compiling and linking](#compiling-and-linking) ·
[The One Definition Rule](#the-one-definition-rule-odr) ·
[Forward declarations](#forward-declarations) ·
[Anonymous namespaces](#anonymous-namespaces) ·
[Header dependency tracking](#header-dependency-tracking--mmd--mp)

**Part B: Basic building blocks**
[Fixed-width integers](#fixed-width-integers-int64_t-size_t-ssize_t) ·
[Integer literal suffixes](#integer-literal-suffixes-ll) ·
[References vs pointers](#references-vs-pointers) ·
[`const`](#const) ·
[Pass by const reference](#pass-by-const-reference) ·
[`nullptr`](#nullptr) ·
[Casts](#casts-static_cast-and-reinterpret_cast) ·
[`enum class`](#enum-class) ·
[`struct` vs `class`](#struct-vs-class) ·
[Default member initializers](#default-member-initializers) ·
[Brace initialization](#brace-initialization-) ·
[Default arguments](#default-arguments) ·
[Ternary operator](#the-ternary-operator--) ·
[Bit flags](#bit-flags---) ·
[Type aliases with `using`](#type-aliases-with-using) ·
[`auto`](#auto) ·
[Range-based `for`](#range-based-for) ·
[Structured bindings](#structured-bindings-c17) ·
[Recursion](#recursion)

**Part C: Classes and object lifetime**
[Constructors and initializer lists](#constructors-and-member-initializer-lists) ·
[`explicit`](#explicit) ·
[Destructors and RAII](#destructors-and-raii) ·
[Deleted copy operations](#deleted-copy-operations--delete) ·
[A surprise: non-copyable *and* non-movable](#a-surprise-these-types-cannot-even-be-moved) ·
[`static` members](#static-members) ·
[`const` member functions](#const-member-functions) ·
[Nested types](#nested-types-and-in-class-forward-declaration) ·
[`new` and `delete`](#new-and-delete-manual-memory) ·
[`std::unique_ptr`](#stdunique_ptr-and-make_unique) ·
[Move semantics and `std::move`](#move-semantics-and-stdmove)

**Part D: The standard library**
[`std::string` as a byte buffer](#stdstring-as-a-byte-buffer) ·
[`std::vector`](#stdvector) ·
[`std::deque`](#stddeque) ·
[`std::unordered_map`](#stdunordered_map) ·
[`std::set` and `std::pair`](#stdset-and-stdpair) ·
[`emplace`, `try_emplace`, `insert_or_assign`](#emplace-try_emplace-insert_or_assign) ·
[Map node handles: `extract`](#map-node-handles-extract-c17) ·
[Iterators and invalidation](#iterators-and-iterator-invalidation) ·
[`std::variant`](#stdvariant-c17) ·
[`std::from_chars` / `std::to_chars`](#stdfrom_chars--stdto_chars-c17) ·
[`<chrono>`](#chrono-clocks-and-durations) ·
[`<random>`](#random-mt19937-and-distributions) ·
[Floating-point specials](#floating-point-specials-inf-and-nan) ·
[Streams vs `printf`](#streams-vs-printf-and-buffering) ·
[Reading a whole file](#reading-a-whole-file-with-istreambuf_iterator)

**Part E: Generic and functional code**
[Function pointers](#function-pointers) ·
[Templates](#function-templates) ·
[Lambdas](#lambdas) ·
[Function-local `static` ("magic statics")](#function-local-static-magic-statics) ·
[`inline` variables and functions](#inline-variables-and-functions-c17)

**Part F: Correctness and undefined behaviour**
[Undefined behaviour](#undefined-behaviour-ub) ·
[Signed overflow](#signed-integer-overflow) ·
[`char` and `<cctype>`](#char-signedness-and-cctype) ·
[Use-after-free traps the code avoids](#use-after-free-traps-the-code-avoids) ·
[Sanitizers](#sanitizers-asan-and-ubsan)

**Part G: C and POSIX inside C++**
[File descriptors](#file-descriptors) ·
[`errno`, `EINTR`, `EAGAIN`](#errno-eintr-eagain) ·
[`::` for global functions](#-the-global-scope-qualifier) ·
[Sockets](#sockets-socket-bind-listen-accept-connect) ·
[Non-blocking I/O and `fcntl`](#non-blocking-io-and-fcntl) ·
[`epoll`](#epoll) ·
[Partial reads and writes](#partial-reads-and-writes) ·
[Signals](#signals-and-volatile-sig_atomic_t) ·
[`fsync`, `rename`, `truncate`](#fsync-fdatasync-rename-truncate)

**Part H: The preprocessor and the test framework**
[Macros](#macros-define) ·
[Stringizing `#` and pasting `##`](#stringizing--and-token-pasting-) ·
[`do { } while (0)`](#do----while-0) ·
[Self-registering tests](#self-registering-tests-static-initialization)

[Cheat sheet: C++17 features used](#cheat-sheet-c17-features-used)

---

# Part A: How a C++ program is built

### Headers and source files
- **In plain words**: a `.h` file *announces* what exists ("there is a function `now_ms()` returning `int64_t`"). A `.cpp` file *provides* it (the actual code). Other files `#include` the header so they know how to call it.
- **In this project**: every module is a pair, for example `server/clock.h` (one declaration) and `server/clock.cpp` (the body).
- **Why here**: when you change `clock.cpp`, only that file is recompiled. Files that include `clock.h` don't need to be.
- **Watch out**: `#include` is literally copy-paste of text. Put as little as possible in headers.

### `#pragma once`
- **In plain words**: "if this header was already pasted into this `.cpp`, skip it this time."
- **In this project**: the first line of every header.
- **Why here**: `database.h` is included by `server.h` *and* `command_helpers.h`. Without the guard, `Database` would be defined twice in the same file, which is a compile error.
- **Watch out**: it's not in the official standard, but every major compiler supports it. The older equivalent is `#ifndef X / #define X / #endif`.

### Compiling and linking
- **In plain words**: building happens in two steps. (1) The **compiler** turns each `.cpp` into an object file (`.o`) on its own, without looking at the others. (2) The **linker** joins the `.o` files and connects each call to the function's actual code.
- **In this project**: the `Makefile` compiles each `.cpp` into `build/release/.../*.o`, then links different sets of objects into four programs (see [diagram 22](5-diagrams.md#22-build-graph)).
- **Why here**: the same `CORE_OBJS` go into the server, the unit tests and the benchmark. The tests check exactly the code that ships.
- **Watch out**: "undefined reference to X" is a *linker* error (the code for X wasn't included). "X was not declared" is a *compiler* error (you're missing a header).

### The One Definition Rule (ODR)
- **In plain words**: inside one program, each function may have exactly one body.
- **In this project**: there are **two** functions called `encode_command`, one in `server/resp_writer.cpp` and one in `client/resp_encoder.cpp`. That's legal because they are never linked into the same program: one goes into the server, the other into the client.
- **Watch out**: if you ever linked `client/*.o` and `server/*.o` together, the linker would report "multiple definition of `encode_command`".

### Forward declarations
- **In plain words**: `class Aof;` says "a class named `Aof` exists; you'll see the details later." You can then use `Aof*` or `Aof&`, but you can't create an `Aof` or call its methods yet.
- **In this project**: `server/commands.h:7-8` (`class Aof; class Database;`), `server/aof.h:7` (`class Database;`).
- **Why here**: `commands.cpp` uses `Aof`, and `aof.cpp` uses `execute_command`. If the two headers included each other, you would get an include cycle. A forward declaration is enough because `CommandContext` only stores a `Database&` and an `Aof*`. Bonus: fewer includes mean faster builds.
- **Watch out**: you need the full definition (`#include`) to access members or to know the object's size.

### Anonymous namespaces
- **In plain words**: `namespace { ... }` makes everything inside it **private to this `.cpp` file**.
- **In this project**: most `.cpp` files, for example the helpers `find_crlf`/`parse_number` in `resp_parser.cpp:3-135` and every `cmd_*` handler.
- **Why here**: `cmd_lists.cpp` and `cmd_strings.cpp` can both have helpers with the same names without clashing at link time. It also tells the reader "nothing outside this file calls this."
- **Watch out**: never put an anonymous namespace in a header. Each file that includes it would get its own separate copy.

### Header dependency tracking (`-MMD -MP`)
- **In plain words**: compiler flags that write a `.d` file listing which headers each `.cpp` used, so `make` knows to recompile when a header changes.
- **In this project**: `Makefile:11` (flags) and the last line, `-include $(wildcard $(BUILD)/*/*.d)`.
- **Watch out**: without it, editing `database.h` would leave old `.o` files using the old layout, which causes mysterious crashes.

---

# Part B: Basic building blocks

### Fixed-width integers (`int64_t`, `size_t`, `ssize_t`)
- **In plain words**: `int64_t` is *exactly* 64 bits on every machine. `size_t` is unsigned and big enough for any object size. `ssize_t` is its signed POSIX cousin, used because `read()` returns `-1` on error.
- **In this project**: `int64_t` for times in milliseconds (`clock.h:10`, `Entry::expire_at`); `size_t` for buffer positions; `ssize_t n = read(...)` in `server.cpp:155`.
- **Why here**: the current time in ms (~1.7 × 10¹²) does not fit in a 32-bit `int`.
- **Watch out**: mixing signed and unsigned values. `size_t(0) - 1` is a huge positive number, not `-1`. That's why the code casts carefully, e.g. `static_cast<size_t>(n)` only *after* checking `n > 0`.

### Integer literal suffixes (`LL`)
- **In plain words**: `512LL` means "this literal is a `long long`".
- **In this project**: `512LL * 1024 * 1024` in `resp_parser.cpp:8`, `1000000000000LL` in `command_helpers.h:21`.
- **Why here**: `512 * 1024 * 1024` with plain `int`s is computed in 32 bits. It happens to fit, but `4 * 1024 * 1024 * 1024` would overflow. Making the first operand `long long` makes the whole calculation 64-bit.

### References vs pointers
- **In plain words**: a reference (`T&`) is another name for an existing object. It can never be null and never be changed to refer to something else. A pointer (`T*`) is an address. It can be `nullptr` and can be changed to point elsewhere.
- **In this project**:
  - References for things that *must* exist: `CommandContext::db` (`Database&`), `std::string& out`.
  - Pointers for things that *might not* exist: `Aof* aof` is `nullptr` when the AOF is off. `Entry* Database::find()` returns `nullptr` for "no such key".
- **Why here**: the type itself documents the rule. `Aof*` says "check for null"; `Database&` says "always there".
- **Watch out**: never keep a reference or pointer to something that may be destroyed. See [use-after-free traps](#use-after-free-traps-the-code-avoids).

### `const`
- **In plain words**: "this can't be changed through this name."
- **In this project**: `const std::string& key` (read-only argument), `const char* data` in the parser (it never modifies the buffer), `const int MAX_EVENTS = 128;` (constants), `inline const char* const ERR_WRONGTYPE` (a constant pointer to constant characters).
- **Why here**: the compiler stops you if you accidentally modify something that should be read-only, and the reader sees the intent.
- **Watch out**: read `const char* const` from right to left: "a const pointer to const char." Neither the pointer nor the text can change.

### Pass by const reference
- **In plain words**: `void f(const std::string& s)` lets `f` read the caller's string **without copying it**.
- **In this project**: nearly every function taking `std::string`, `Args` or `Config`.
- **Why here**: a command's arguments may be megabytes (512 MB maximum per argument). Copying them for every call would waste time and memory.
- **Watch out**: small types (`int`, `double`, `size_t`) are passed by value. Copying them is as cheap as passing an address.

### `nullptr`
- **In plain words**: the "points to nothing" value, with its own type (unlike the old `NULL`, which was just `0`).
- **In this project**: `return nullptr;` in `Database::find`; `Aof* aof` set to `nullptr` during replay.

### Casts (`static_cast` and `reinterpret_cast`)
- **In plain words**: explicit type conversions. `static_cast` is for sensible conversions (number types, `size_t` ↔ `long long`). `reinterpret_cast` says "treat these bytes as a different type," which is dangerous and only used where an API requires it.
- **In this project**: `static_cast<long long>(list->size())` before replying; `reinterpret_cast<sockaddr*>(&addr)` in `net.cpp:38`, because the C socket API takes a generic `sockaddr*`.
- **Why here**: C-style casts `(int)x` silently do *any* of these. Named casts make each conversion visible and searchable.

### `enum class`
- **In plain words**: a named set of options, like `ParseStatus::Ok`, `Incomplete`, `Error`. The `class` part means the names stay inside the enum and don't silently convert to integers.
- **In this project**: `ParseStatus` (`resp_parser.h:8`), `FsyncPolicy` (`aof.h:10`), `Reply::Type` (`client/reply.h:9`).
- **Why here**: a function returning `ParseStatus` is far clearer than one returning `0/1/-1`. You can't mix up a `ParseStatus` with an `FsyncPolicy`.
- **Watch out**: with a plain `enum`, `if (status == 1)` compiles; with `enum class` it doesn't. That's the point.

### `struct` vs `class`
- **In plain words**: they're identical, except that members are public by default in a `struct` and private by default in a `class`.
- **In this project (convention)**: `struct` for plain bundles of data (`Connection`, `Config`, `Entry`, `CommandContext`, `Reply`); `class` for objects that protect their own rules (`Database`, `SkipList`, `Aof`, `Server`).
- **Why here**: `SkipList` must keep its spans consistent, so nobody outside may touch `head_`. A `Config` has no rules to protect; it's just values.

### Default member initializers
- **In plain words**: giving a member its starting value right where it's declared: `int fd = -1;`.
- **In this project**: `connection.h` (`fd = -1`, `output_sent = 0`), `config.h` (all defaults), `skiplist.h:66-68` (`level_ = 1`, `rng_{12345}`).
- **Why here**: every constructor automatically starts from safe values. `Config config;` in `main.cpp` *is* "the defaults" without any extra code.

### Brace initialization `{}`
- **In plain words**: `T x{};` creates `x` with every member set to zero/empty.
- **In this project**: `sockaddr_in addr{};` (`net.cpp:33`), `epoll_event event{};`, `struct sigaction action {};`, `addrinfo hints{};`.
- **Why here**: these are C structs with many fields. Without `{}` the unused fields would contain random garbage, and the kernel may read them.
- **Also**: `Node{member, score, std::vector<Level>(new_level)}` (`skiplist.cpp:64`) fills a struct's fields in order. `return {name, function};` builds the return value from a list.

### Default arguments
- **In plain words**: a parameter can have a value that's used when the caller leaves it out.
- **In this project**: `format_reply(const Reply& reply, const std::string& indent = "")` (`client/reply_printer.h:15`). Callers write `format_reply(r)`; the recursive call passes a bigger indent.
- **Watch out**: write the default only in the header declaration, not again in the `.cpp`.

### The ternary operator `? :`
- **In plain words**: `cond ? a : b` is an inline if/else that produces a value.
- **In this project**: `data[0] == '*' ? parse_array(...) : parse_inline(...)` (`resp_parser.cpp:143`), `in_ms ? ms_left : (ms_left + 500) / 1000` (`cmd_keys.cpp:97`).

### Bit flags (`&`, `|`, `|=`)
- **In plain words**: several on/off options packed into one integer, one bit each. `|` combines them, and `&` tests one.
- **In this project**: `flags & EPOLLIN` (`server.cpp:116`), `event.events |= EPOLLOUT` (`server.cpp:240`), `O_WRONLY | O_CREAT | O_APPEND` (`aof.cpp:75`), `flags | O_NONBLOCK` (`net.cpp:13`).
- **Watch out**: `F_SETFL` *replaces* all the flags, so `net.cpp` first reads the current flags (`F_GETFL`) and adds `O_NONBLOCK` to them, rather than overwriting the others.

### Type aliases with `using`
- **In plain words**: a new, shorter or more meaningful name for an existing type.
- **In this project**: `using Args = std::vector<std::string>;`, `using List = std::deque<std::string>;`, `using Value = std::variant<...>;`, `using Handler = void (*)(CommandContext&, const Args&);`.
- **Why here**: `Args` says *what it is* (a command's arguments), not just how it's stored. Changing `List` to another container later means changing one line.

### `auto`
- **In plain words**: "compiler, work out the type from the right-hand side."
- **In this project**: `auto it = data_.find(key);` (the real type is `std::unordered_map<std::string, Entry>::iterator`), `auto node = data_.extract(from);`.
- **Watch out**: `auto x = some_reference;` makes a **copy**. Write `auto&` or `const auto&` to avoid it. The loops use `const auto&`.

### Range-based `for`
- **In plain words**: `for (const std::string& key : keys)` means "for each element, in order."
- **In this project**: everywhere, for example `cmd_keys.cpp:48`, `aof.cpp:125`.
- **Watch out**: don't add or remove elements of the container inside such a loop. Iterators may become invalid.

### Structured bindings (C++17)
- **In plain words**: unpack a pair or struct into named variables in one line.
- **In this project**: `for (const auto& [key, entry] : data_)` (`database.cpp:110`), `for (const auto& [member, score] : items)` (`cmd_zsets.cpp:127`), `for (auto& [fd, conn] : connections_)` (`server.cpp:267`).
- **Why here**: `entry` reads much better than `it->second`.

### Recursion
- **In plain words**: a function that calls itself to handle a smaller piece of the same problem.
- **In this project**: `ReplyReader::read_reply` (`client/reply_reader.cpp:97`) and `format_reply` (`reply_printer.cpp:54`). Arrays can contain arrays, so each element is read and printed with the same function.
- **Watch out**: very deep nesting could overflow the stack. Fine for a client talking to a trusted server.

---

# Part C: Classes and object lifetime

### Constructors and member initializer lists
- **In plain words**: a constructor sets up a new object. The `: member(value)` list after it initializes members *directly*, instead of creating them empty and assigning later.
- **In this project**: `Server::Server(const Config& config) : config_(config) {}` (`server.cpp:44`), `Aof::Aof(...) : path_(path), policy_(policy) {}`, `Database::Database() : clock_(now_ms) {}`.
- **Watch out**: members are initialized in the order they are *declared in the class*, not the order in the list. `-Wall` warns when the two differ.

### `explicit`
- **In plain words**: stops a one-argument constructor from being used as a hidden automatic conversion.
- **In this project**: `explicit Server(const Config&)`, `explicit ReplyReader(int fd)`.
- **Why here**: without `explicit`, a function expecting a `ReplyReader` would silently accept *any* `int` and build one. That's a bug waiting to happen.

### Destructors and RAII
- **In plain words**: RAII ("Resource Acquisition Is Initialization") means an object that gets a resource (file, socket, memory) **frees it in its destructor**. The destructor runs automatically when the object goes out of scope, even on an early `return`.
- **In this project**:
  | Class | Resource | Destructor does |
  |---|---|---|
  | `TcpConnection` | socket fd | `close()` (`tcp_connection.cpp:10`) |
  | `Aof` | file fd + buffer | `flush()`, `fsync`, `close` (`aof.cpp:65`) |
  | `SkipList` | every `new`ed node | walks level 0 and `delete`s each (`skiplist.cpp:15`) |
  | `Server` | sockets, epoll, AOF | `shutdown()` (`server.cpp:46`) |
  | `std::unique_ptr<Aof>` | the `Aof` object | `delete`s it |
- **Why here**: `main` just returns, and everything is cleaned up in the right order. The unit tests use `{ ... }` blocks so the `Aof` destructor flushes the file *before* the test reads it back (`test_aof.cpp:35-48`).
- **Watch out**: `Server::shutdown()` is called both from `run()` and from the destructor, so it guards itself (`if (listen_fd_ < 0) return;`). Cleanup must be safe to run twice.

### Deleted copy operations (`= delete`)
- **In plain words**: `SkipList(const SkipList&) = delete;` means "this type cannot be copied; any attempt is a compile error."
- **In this project**: `SkipList` (`skiplist.h:30-31`), `Aof` (`aof.h:23-24`), `TcpConnection`.
- **Why here**: a `SkipList` owns raw pointers. A default copy would copy the *pointers*, not the nodes, so two lists would share the same nodes, and both destructors would `delete` them: a **double free**. A copied `Aof` or `TcpConnection` would `close()` the same fd twice. Deleting the copy makes the bug impossible instead of just unlikely. This is the "rule of three": if you write a destructor, decide what copying should do.

### A surprise: these types cannot even be moved
This is a subtle point most people miss. It is verified with `std::is_move_constructible_v` (all `false`):

- Declaring a copy constructor (even as `= delete`) **stops the compiler from generating a move constructor**. So `SkipList` is neither copyable nor movable.
- `SortedSet` contains a `SkipList`, so it isn't movable either. `Value` (the variant) contains `SortedSet`, so it isn't movable. `Entry` contains `Value`, so it isn't movable.

**So how does the code store entries at all?** It never needs to move them:
- `std::unordered_map` is **node-based**: each element lives in its own heap node that never moves, even when the table grows (rehashes). Only the pointers to nodes are reorganised.
- Values are built **in place**: `data_.try_emplace(key)` and `entry.value.emplace<T>()` construct directly inside the node.
- `RENAME` uses `extract()`, which moves the *node handle* (a pointer), not the `Entry`.

**What would break**: `std::vector<Entry>`, `Entry copy = *entry;`, or returning an `Entry` by value would not compile. That's a nice illustration of how C++'s type system enforces ownership rules.
**What you could add**: a proper move constructor for `SkipList` (take `head_`, leave the source empty). That's a good exercise.

### `static` members
- **In plain words**: a `static` member belongs to the *class*, not to each object. A `static` member function has no `this`.
- **In this project**: `static const int MAX_LEVEL = 32;` (`skiplist.h:59`) is used as a C-array size (`Node* update[MAX_LEVEL];`). `static bool comes_before(...)` (`skiplist.h:62`) doesn't need any list data, just two nodes' values.

### `const` member functions
- **In plain words**: a method marked `const` after its parameter list promises not to modify the object. Only `const` methods can be called on a `const` object.
- **In this project**: `size() const`, `Database::is_expired(...) const`, `Database::keys() const`, `SkipList::rank(...) const`, `SortedSet::range(...) const`.
- **Why here**: `aof.cpp:48` calls `zset->range(...)` on a `const SortedSet*`. That only compiles because `range` is `const`.

### Nested types and in-class forward declaration
- **In plain words**: a type declared *inside* a class, private to it.
- **In this project**: `SkipList::Node` and `SkipList::Level` (`skiplist.h:48-57`). Note the trick: `struct Node;` is declared first, because `Level` needs `Node*` and `Node` needs `std::vector<Level>`. Each refers to the other.
- **Why here**: nobody outside the skip list should know nodes exist.

### `new` and `delete` (manual memory)
- **In plain words**: `new` creates an object on the heap and returns a pointer. You must `delete` it exactly once later.
- **In this project**: only in the skip list (`skiplist.cpp:12, 64, 104, 20`). Everything else uses containers and smart pointers.
- **Why here**: each node has a different number of levels and is linked from several other nodes. That's a graph, which smart pointers handle poorly (and `shared_ptr` would cost memory and speed). Because the code is contained in one class with deleted copying, ASan, and a randomized test, manual memory is defensible.
- **Watch out**: forgetting `delete` leaks memory. Deleting twice corrupts the heap. Using after `delete` reads garbage. All three are caught by AddressSanitizer in CI.

### `std::unique_ptr` and `make_unique`
- **In plain words**: a pointer that **owns** its object and deletes it automatically. It can't be copied (only one owner), and it can be empty.
- **In this project**: `std::unique_ptr<Aof> aof_;` (`server.h:43`), created with `std::make_unique<Aof>(path, policy)` (`server.cpp:59`). `aof_.get()` hands a plain, non-owning `Aof*` to `execute_command`.
- **Why here**: "the AOF may not exist" (`--no-aof`) is naturally expressed as an empty `unique_ptr`. `if (aof_)` checks for it.
- **Watch out**: the raw pointer from `.get()` must not outlive the `unique_ptr`. Here the `Server` (which owns it) outlives every command.

### Move semantics and `std::move`
- **In plain words**: moving means "take the contents and leave the source empty" instead of copying. For a 1 MB string, a move copies about 3 pointers; a copy copies 1 MB. `std::move(x)` says "you may steal from `x`."
- **In this project**:
  - `value = std::move(list->front()); list->pop_front();` (`cmd_lists.cpp:43-44`): take the string out of the list without copying, then remove the now-empty slot.
  - `data_.insert(std::move(node));` (`database.cpp:61`): node handles can only be moved.
- **Watch out**: after `std::move(x)`, `x` is valid but has unspecified contents. Only destroy or reassign it. The code pops the moved-from element immediately.

---

# Part D: The standard library

### `std::string` as a byte buffer
- **In plain words**: `std::string` holds any bytes, including `'\0'` and `\r\n`. It is not limited to readable text.
- **In this project**: `Connection::input` and `output`, `Aof::buffer_`, every value stored. `conn.input.append(buffer, n)` (`server.cpp:166`) appends exactly `n` raw bytes. `args.emplace_back(data + pos, bulk_len)` builds a string from a pointer and a length.
- **Why here**: RESP is binary safe. A value can be an image. Using `(pointer, length)` constructors, never C-string functions like `strlen`, keeps every byte, including zeros.
- **Watch out**: `input.erase(0, pos)` shifts all remaining bytes, which costs O(remaining). That's why `process_input` erases **once per read**, not once per command (`server.cpp:202-203`).

### `std::vector`
- **In plain words**: a growable array. Fast to index and append at the end, slow to insert at the front.
- **In this project**: `Args`, the skip list's `levels`, `Reply::elements`, `range()` results.
- **Fun fact**: `struct Reply` contains `std::vector<Reply>`, a vector of the type being defined. C++17 explicitly allows `std::vector` of an incomplete type, which is what makes the recursive reply tree possible.

### `std::deque`
- **In plain words**: like a vector, but fast to add and remove at **both** ends. It's stored as several fixed-size blocks.
- **In this project**: `using List = std::deque<std::string>;` (`database.h:14`).
- **Why here**: `LPUSH`/`LPOP` work at the front. `vector::insert(begin())` shifts every element. The micro benchmark shows deque is about 1000-3500× faster for 50 000 front-pushes (exact numbers vary by machine).
- **Watch out**: erasing in the middle (`LREM`) is still O(n), and invalidates iterators. See [iterators](#iterators-and-iterator-invalidation).

### `std::unordered_map`
- **In plain words**: a hash table. It maps keys to values with O(1) average lookup. Its iteration order is arbitrary.
- **In this project**: the keyspace (`data_`), `Hash`, `SortedSet::scores_`, `Server::connections_`, the command table.
- **Why here**: a key-value store mostly does "look up by exact key," which is exactly what hash tables are best at.
- **Two properties the code relies on**:
  1. **References and pointers to elements stay valid when the map grows**, because each element lives in its own node. Only *iterators* are invalidated by a rehash. That's why `Database::find` can return an `Entry*`, and `Server` can hold `Connection&` while other clients are added.
  2. Iteration order is arbitrary, so `HGETALL` and `KEYS` return items in no particular order. Real Redis behaves the same.

### `std::set` and `std::pair`
- **In plain words**: `std::set` keeps unique elements **sorted** (it's a red-black tree): O(log n) insert, erase and find, and `begin()` is always the smallest. `std::pair<A, B>` compares by `first`, then by `second`.
- **In this project**: `std::set<std::pair<int64_t, std::string>> expiry_index_` (`database.h:82`).
- **Why here**: sorting by `(expire_time, key)` puts the soonest-to-expire key at `begin()`. Active expiry just pops from the front until it finds a future time. Including the key in the pair makes entries unique, even when two keys expire in the same millisecond.

### `emplace`, `try_emplace`, `insert_or_assign`
- **In plain words**:
  - `emplace_back(args...)` builds the element *inside* the container from constructor arguments: no temporary copy.
  - `try_emplace(key)` (C++17) inserts a default-constructed value only if the key is missing.
  - `insert_or_assign(k, v)` (C++17) inserts or overwrites, and returns `{iterator, bool was_inserted}`.
  - `variant.emplace<T>(args...)` destroys the current alternative and builds a `T` in its place, returning a reference to it.
- **In this project**: `data_.try_emplace(key).first->second` (`database.cpp:35`); `hash->insert_or_assign(...).second` counts *new* fields for `HSET` (`cmd_hashes.cpp:25`); `entry.value.emplace<std::string>(args[2])` (`cmd_strings.cpp:47`).
- **Why here**: `Entry` can't be moved or copied (see above), so building in place is the only option, and it's also the fastest.

### Map node handles: `extract` (C++17)
- **In plain words**: `map.extract(key)` removes an element from the map **without destroying it**, giving you a "node handle". You can change the node's key and insert it back. No value is copied or moved.
- **In this project**: `Database::rename` (`database.cpp:59-61`).
- **Why here**: renaming a key holding a 1-million-element list is O(1). The list's memory never moves.

### Iterators and iterator invalidation
- **In plain words**: an iterator is a bookmark into a container. Some operations make existing bookmarks invalid. Using an invalid one is undefined behaviour.
- **In this project**:
  - `it = list->erase(it);` (`cmd_lists.cpp:157`): `erase` returns a valid iterator to the next element. This is *the* standard idiom for erasing while looping.
  - The `LREM` negative-count branch uses indexes (`list->begin() + i`) and walks backwards, so earlier indexes stay correct after each erase.
- **Watch out**: `for (auto it = c.begin(); it != c.end(); ++it) if (...) c.erase(it);` is a classic bug: `it` is dead after `erase`.

### `std::variant` (C++17)
- **In plain words**: a type-safe union. It holds **exactly one** of several types at a time and remembers which one.
- **In this project**: `using Value = std::variant<std::string, List, Hash, SortedSet>;` (`database.h:19`).
  - `std::get_if<T>(&entry->value)` returns a `T*` if the variant holds a `T`, otherwise `nullptr`. Used in `find_typed<T>` (`command_helpers.h:44`) and in the AOF rewrite.
  - `value.index()` gives 0..3, which `type_name()` maps to "string", "list", "hash", "zset" (`database.cpp:7`).
  - `value.emplace<T>()` switches the type.
- **Why here**: one key can only ever have one type. The old design had separate maps per type, so a key could be a string *and* a list at once. With `variant` that state can't exist, and `WRONGTYPE` checking becomes one line.
- **Watch out**: a variant is as big as its **largest** alternative. Here that's `SortedSet` at about 5 KB (because of `std::mt19937`), so *every* key costs about 5 KB, even a tiny string. See [known issue #1](7-defense-guide.md#known-issue-1-every-key-costs-about-5-kb). The usual fix is to store big, rare alternatives behind a pointer (`std::unique_ptr<SortedSet>`).

### `std::from_chars` / `std::to_chars` (C++17)
- **In plain words**: the fastest standard way to convert text ↔ numbers. No locale, no exceptions, no memory allocation. They report errors through a result struct.
- **In this project**:
  - `parse_integer` (`command_helpers.cpp:18-24`): succeeds only if `ec == std::errc()` (no error) **and** `ptr == end` (every character was used). So `"12abc"` and `" 12"` are rejected, matching Redis.
  - `format_double` (`resp_writer.cpp:57-64`): `std::to_chars` writes the **shortest text that reads back as exactly the same double**. So `1.1` prints as `"1.1"`, not `"1.1000000000000001"`. That makes AOF rewrites exact.
- **Why not `atoi`/`stoll`?** `atoi("12abc")` silently returns 12, and `atoi("abc")` returns 0. `stoll` throws exceptions and accepts leading spaces.
- **Watch out**: floating-point `to_chars` needs GCC 11 or newer.

### `<chrono>`: clocks and durations
- **In plain words**: type-safe time. `system_clock` is wall-clock time (it can jump if someone changes the system clock). `steady_clock` only moves forward (good for measuring durations).
- **In this project**: `now_ms()` uses `system_clock` (`clock.cpp:7`) because expiry times are written to the AOF and must mean the same moment after a restart. The benchmark uses `steady_clock` (`micro_bench.cpp:21`) because it measures elapsed time.
- **Watch out**: if the machine's clock jumps forward an hour, keys with TTLs expire early. Real Redis has the same trade-off.

### `<random>`: `mt19937` and distributions
- **In plain words**: `std::mt19937` is a high-quality pseudo-random generator (the "Mersenne Twister"). A distribution shapes its raw output, for example `uniform_real_distribution(0.0, 1.0)` gives evenly spread doubles.
- **In this project**: the skip list's `random_level()` (`skiplist.cpp:25-30`) "flips a coin" with probability 1/4 per extra level. It's seeded with a fixed `12345` so the list has the same shape every run (easier debugging). The randomized test uses `mt19937 rng(42)` so failures are reproducible.
- **Watch out**: an `mt19937` object holds about 5 KB of internal state. Embedding one in every `SkipList` is the root of [known issue #1](7-defense-guide.md#known-issue-1-every-key-costs-about-5-kb). A single shared generator would do the job.

### Floating-point specials: `inf` and `NaN`
- **In plain words**: `double` can be `+inf`, `-inf`, or `NaN` ("not a number"). `NaN` is not equal to anything, *including itself*.
- **In this project**: `parse_score` accepts `+inf`, `-inf` and `inf` (valid Redis scores) but rejects `NaN` (`command_helpers.cpp:26-36`). `format_double` prints infinity as `inf`/`-inf`.
- **Why reject NaN**: the skip list orders by `<`. With `NaN`, `a < b` and `b < a` are both false, which would break sorting and corrupt the structure.

### Streams vs `printf`, and buffering
- **In plain words**: `std::cout << x` (type-safe streams) and `printf("%d", x)` (C-style) both write output. Output is *buffered*: it collects in memory and is written later.
- **In this project**: the server uses `printf`/`perror`; the client uses `std::cout`/`std::cerr`. `setvbuf(stdout, nullptr, _IOLBF, 0)` (`server/main.cpp:57`) makes stdout write after every line, even when redirected to a log file. Otherwise test logs would show nothing until the buffer filled. The client prints the prompt with `<< std::flush` so it appears before waiting for input.

### Reading a whole file with `istreambuf_iterator`
- **In this project**: `std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());` (`aof.cpp:158`).
- **In plain words**: build a string from "every character from the file's start to its end." The extra parentheses around the first argument avoid C++'s "most vexing parse", where the line would otherwise be read as a *function declaration*.

---

# Part E: Generic and functional code

### Function pointers
- **In plain words**: a variable that holds a function, so you can choose at runtime which function to call.
- **In this project**:
  - `using Handler = void (*)(CommandContext& ctx, const Args& args);` (`commands.h:20`). The command table maps `"GET"` → `{cmd_get, 2}`, and `execute_command` calls `command.handler(ctx, args)`.
  - `int64_t (*clock_)();` in `Database` (`database.h:83`), set to `now_ms` by default. Tests swap in a fake clock with `set_clock(...)` to "travel in time" without sleeping.
  - `void (*function)()` in the test registry.
- **Why here**: dispatch through a table is O(1) and adding a command is one line. The swappable clock is *dependency injection*: the database doesn't care where time comes from.
- **Alternative**: `std::function` can also hold lambdas with captured state, but it's heavier. Plain functions are all that's needed here.

### Function templates
- **In plain words**: write a function once with a placeholder type `T`; the compiler generates a version for each type you use.
- **In this project**:
  - `find_typed<T>` and `find_or_create<T>` (`command_helpers.h:39-56`): one implementation for `std::string`, `List`, `Hash` and `SortedSet`. Calling `find_typed<List>(db, key, wrong_type)` returns a `List*`.
  - `time_ms(Work work)` in the benchmark accepts any callable.
  - `printable(const T&)` in the test framework, alongside non-template overloads for strings.
- **Why here**: without templates, every handler would repeat the "look up, check type, report WRONGTYPE" logic four times over.
- **Watch out**: templates must be fully defined in the header, because the compiler needs the body when it sees `find_typed<List>`. Overload rule used by `printable`: an exact non-template match (`std::string`) wins over the template.

### Lambdas
- **In plain words**: an unnamed function written inline. `[&]` means it can use (by reference) the variables around it.
- **In this project**:
  - `static const CommandTable table = [] { CommandTable t; ...; return t; }();` (`commands.cpp:10-18`). The trailing `()` calls the lambda immediately. This is the "immediately invoked lambda" pattern for initialising a `const` value with several statements.
  - `time_ms([&] { ... })` in `micro_bench.cpp` passes the code to be timed.

### Function-local `static` ("magic statics")
- **In plain words**: a `static` variable inside a function is created the **first time** the function runs and then lives until the program ends. Since C++11, this first-time creation is guaranteed to happen exactly once, even with threads.
- **In this project**: the command table (`commands.cpp:10`); `all_tests()` and `failure_count()` in `test_framework.h`.
- **Why here**: the table is built on first use, with no global-initialisation-order problems. The test registry *must* work this way, because tests register themselves before `main()` runs (see [self-registering tests](#self-registering-tests-static-initialization)).

### `inline` variables and functions (C++17)
- **In plain words**: `inline` on something defined in a header means "this may appear in many `.cpp` files; treat them all as the same single thing." It's not about speed.
- **In this project**: `inline const char* const ERR_WRONGTYPE = ...;` (`command_helpers.h:13`), and the `inline` functions in `test_framework.h`.
- **Why here**: `all_tests()` must return **the same** vector to every test file. Without `inline`, defining a function in a header that's included by several `.cpp` files breaks the One Definition Rule.

---

# Part F: Correctness and undefined behaviour

### Undefined behaviour (UB)
- **In plain words**: things the C++ standard says you must never do, like reading freed memory, signed overflow, or out-of-bounds indexing. If you do, *anything* may happen: a crash, wrong results, or worst of all, appearing to work until it doesn't. The optimiser is allowed to assume UB never happens, which can delete your safety checks.
- **In this project**: guarded against everywhere below, and checked at runtime by the sanitizers.

### Signed integer overflow
- **In plain words**: `LLONG_MAX + 1` is UB for signed integers. It does **not** reliably wrap around.
- **In this project**: `INCRBY`/`HINCRBY` check **before** adding: `if ((delta > 0 && current > LLONG_MAX - delta) || (delta < 0 && current < LLONG_MIN - delta))` (`cmd_strings.cpp:79`). `DECRBY` rejects `LLONG_MIN` because `-LLONG_MIN` overflows (`cmd_strings.cpp:107`). `MAX_EXPIRE_AMOUNT` keeps `now + amount * 1000` far from overflow.
- **Watch out**: `if (current + delta < current)` is the wrong check. The addition already happened, and the compiler may delete the `if` entirely.

### `char` signedness and `<cctype>`
- **In plain words**: `char` may be signed, so byte `0xE9` can be `-23`. `std::toupper(-23)` is UB.
- **In this project**: `std::toupper(static_cast<unsigned char>(c))` (`command_helpers.cpp:9`), and `for (unsigned char c : text)` in `reply_printer.cpp:12`.
- **Why here**: keys and values are arbitrary bytes. A UTF-8 command name like `"gét"` must not trigger UB.

### Use-after-free traps the code avoids
These are the subtle spots. Know them; they make great interview stories.

1. **`remove_expired` copies the key first** (`database.cpp:95`): `std::string key = first->second;`. `first->second` lives *inside* the set element that `remove()` erases. This copy is **defensive**: today, `remove()` happens to copy `key` into a temporary `{time, key}` pair *before* it erases anything, so a reference would still work. (I tried it: ASan stays quiet.) But it would turn into a use-after-free the moment someone edits `remove()` to use `key` after the erase. Copying makes the caller safe however the callee changes. That's a good point to make in an interview: "safe by construction, not by luck."
2. **`on_readable` returns `false` after closing** (`server.cpp:156-171`). `close_connection` erases the `Connection` from the map, so the `conn` reference the caller holds now points to freed memory. The `bool` tells `event_loop` not to touch `conn` again (`server.cpp:117`).
3. **`event_loop` re-looks up each fd** (`server.cpp:107-108`). If an earlier event in the same batch closed this fd, `find` fails and it's skipped.
4. **`SkipList::~SkipList` saves `next` before `delete node`** (`skiplist.cpp:19-20`).
5. **`Aof::rewrite` reopens its fd** after `rename()`. The old fd still refers to the replaced (now unlinked) file, so writing to it would silently lose data.

### Sanitizers (ASan and UBSan)
- **In plain words**: compiler options that add runtime checks. **AddressSanitizer** catches out-of-bounds access, use-after-free, double free and leaks. **UndefinedBehaviorSanitizer** catches signed overflow, invalid shifts, misaligned pointers and more. Programs run about 2× slower but fail loudly at the exact bad line.
- **In this project**: `make SAN=1 test` (`Makefile:14-20`). CI runs every test both ways (`.github/workflows/ci.yml`).
- **Why here**: hand-written pointer code (the skip list) plus byte parsing is exactly where memory bugs hide. The randomized 20 000-operation test under ASan is strong evidence the skip list is memory-safe.

---

# Part G: C and POSIX inside C++

### File descriptors
- **In plain words**: Linux identifies every open file, socket and epoll instance by a small integer, the fd. `0`/`1`/`2` are stdin/stdout/stderr. `-1` means "none" or "error".
- **In this project**: `int fd` throughout. Initialised to `-1` so "not opened yet" is clear.

### `errno`, `EINTR`, `EAGAIN`
- **In plain words**: when a system call fails it returns `-1` and stores the reason in the global `errno`.
  - `EINTR`: "a signal interrupted me, try again." Not a real error.
  - `EAGAIN` / `EWOULDBLOCK`: "a non-blocking call has nothing to do right now." Not a real error either.
- **In this project**: every loop around `read`, `send`, `write`, `accept`, `recv` and `epoll_wait` handles these two cases specially (for example `server.cpp:94, 131, 161, 215-216`; `aof.cpp:23`).
- **Watch out**: check `errno` immediately after the failing call. Any other call, even `printf`, may change it.

### `::` the global-scope qualifier
- **In plain words**: `::open(...)` means "the `open` defined at global scope", i.e. the POSIX function, not some other `open` that's closer in scope.
- **In this project**: inside `Aof::open()`, the code calls `::open(path_.c_str(), ...)` (`aof.cpp:75`). Plain `open(...)` would mean `Aof::open` itself, a recursive call with the wrong arguments that wouldn't compile. Similarly `::close`, `::write` and `::rename`. `TcpConnection::close()` calls `::close(fd_)` for the same reason.

### Sockets: `socket`, `bind`, `listen`, `accept`, `connect`
- **In plain words**: the server creates a socket, `bind`s it to a port, `listen`s, then `accept`s each client (getting a new fd per client). The client creates a socket and `connect`s.
- **In this project**: `net.cpp:21-54` (server side), `tcp_connection.cpp:14-41` (client side, using `getaddrinfo` to support host names, IPv4 and IPv6).
  - `SO_REUSEADDR` allows an immediate restart on the same port.
  - `htons`/`htonl` convert numbers to network byte order (big-endian).
  - `TCP_NODELAY` disables Nagle's algorithm, so small replies are sent immediately instead of waiting to be merged.

### Non-blocking I/O and `fcntl`
- **In plain words**: normally `read()` waits (blocks) until data arrives. In non-blocking mode it returns immediately with `EAGAIN` instead. That's required for one thread to serve many sockets.
- **In this project**: `set_nonblocking` (`net.cpp:10-14`), applied to the listening socket and to every client.

### `epoll`
- **In plain words**: a Linux API for "watch these thousands of fds and tell me which are ready." `epoll_create1` makes the watch list, `epoll_ctl` adds, changes or removes fds, and `epoll_wait` sleeps until something is ready (or a timeout passes).
- **In this project**: `server.cpp`. The server uses **level-triggered** mode (the default): an fd keeps being reported while it's ready. `event.data.fd` stores which fd an event is for.
- See [Layer 1 §4](1-basics.md#4-serving-many-clients-blocking-vs-non-blocking-threads-vs-an-event-loop) and [diagram 6](5-diagrams.md#6-the-event-loop-one-iteration).

### Partial reads and writes
- **In plain words**: `write(fd, buf, 1000)` may write only 300 bytes. `read` may return less than you asked for. You must loop.
- **In this project**: `write_all` (`aof.cpp:18-29`), `send_all` (`tcp_connection.cpp:43-55`), `output_sent` tracking in the server (`server.cpp:206-219`), and `ReplyReader::read_exact`.
- **`MSG_NOSIGNAL`** on `send()`: if the peer has gone away, return `EPIPE` instead of raising `SIGPIPE`, which would kill the process.

### Signals and `volatile sig_atomic_t`
- **In plain words**: a signal (Ctrl+C = `SIGINT`) interrupts your program *between any two instructions* to run a handler. Inside a handler almost nothing is safe (no `printf`, no `malloc`, no locks), because you may have interrupted those very functions halfway.
- **In this project**: `volatile sig_atomic_t g_stop_requested` (`server.cpp:20`). The handler only sets it to `1`. `sig_atomic_t` guarantees the write is a single, untearable operation. `volatile` stops the compiler from caching the value in a register, so the loop really re-reads it.
  - `sigaction` without `SA_RESTART` makes `epoll_wait` return `EINTR` right away, so shutdown is prompt.
  - `signal(SIGPIPE, SIG_IGN)` ignores the "write to closed socket" signal.
- **Watch out**: `volatile` is **not** a threading tool. It's correct here only because the signal handler runs on the same thread. Between threads you'd use `std::atomic`.

### `fsync`, `fdatasync`, `rename`, `truncate`
- **In plain words**:
  - `write` copies to the kernel's page cache. `fsync` forces it to the physical disk. `fdatasync` is the same but skips unneeded metadata like access times (a bit faster).
  - `rename(tmp, final)` replaces a file **atomically**: other processes see either the old file or the new one, never half of one.
  - `truncate(path, n)` cuts a file down to `n` bytes.
  - `O_APPEND` makes every `write` go to the end of the file.
- **In this project**: `aof.cpp` uses all of them (see [diagrams 15-17](5-diagrams.md#15-aof-write-path-and-fsync-timing)).

---

# Part H: The preprocessor and the test framework

### Macros (`#define`)
- **In plain words**: text substitution before compiling. `CHECK(x)` is replaced by the macro's body with `x` pasted in.
- **In this project**: only in `tests/test_framework.h`, for `TEST`, `CHECK` and `CHECK_EQ`.
- **Why a macro and not a function**: a macro can capture the *source text* of the condition and the `__FILE__`/`__LINE__` where it was written. A function can't. That's how a failure prints something like `tests/test_commands.cpp:<line>: CHECK(x == 3) failed`, pointing straight at the failing line.

### Stringizing `#` and token pasting `##`
- **In plain words**: inside a macro, `#x` turns the argument into a string literal (`"x == 3"`), and `a##b` glues two tokens into one identifier.
- **In this project**: `#condition` and `#actual` produce the readable failure messages. `registrar_##name` creates a unique variable name per test, like `registrar_string_commands`.

### `do { ... } while (0)`
- **In plain words**: wrapping a multi-statement macro this way makes it act like **one** statement, so `if (a) CHECK(b); else ...` works correctly and still requires a semicolon.
- **In this project**: `CHECK` and `CHECK_EQ`.
- **Bonus**: `CHECK_EQ` stores `(actual)` in a local variable first, so an expression with side effects (like `z.add(...)`) is evaluated only once.

### Self-registering tests (static initialization)
- **In plain words**: `TEST(foo) { ... }` expands to three things: a declaration of `foo`, a global `TestRegistrar registrar_foo("foo", foo);`, and the start of `foo`'s body. Global objects are constructed **before `main()` runs**, and the registrar's constructor pushes the test into `all_tests()`. `main` then just loops over the list.
- **In this project**: `test_framework.h:30-56`, `test_main.cpp`.
- **Why `all_tests()` is a function with a `static` inside**: the order in which globals in *different* `.cpp` files are initialised is unspecified (the "static initialization order fiasco"). A function-local static is created on first call, so it's guaranteed to exist whenever the first registrar uses it.

---

## Cheat sheet: C++17 features used

| Feature | Where | One-line purpose |
|---|---|---|
| `std::variant`, `std::get_if`, `emplace<T>` | `database.h`, `command_helpers.h` | one key, exactly one type |
| `std::from_chars` / `std::to_chars` | `command_helpers.cpp`, `resp_writer.cpp`, client | strict, fast, exact number conversion |
| structured bindings `[a, b]` | loops over maps | readable pair unpacking |
| `try_emplace`, `insert_or_assign` | `database.cpp`, `cmd_hashes.cpp` | in-place insert, and "was it new?" |
| `extract` node handles | `Database::rename` | O(1) rename without moving the value |
| `inline` variables | `command_helpers.h` | constants defined once in a header |
| `std::vector` of an incomplete type | `client/reply.h` | recursive `Reply` tree |
| `-std=c++17` | `Makefile` | turns all of the above on |

**C++11/14 features also used**: `auto`, range-for, lambdas, `nullptr`, `enum class`, `= delete`, `std::unique_ptr`, `std::make_unique` (C++14), move semantics, `<chrono>`, `<random>`, default member initializers, thread-safe function-local statics.

---

Next: [Layer 7: Defending every decision](7-defense-guide.md)
