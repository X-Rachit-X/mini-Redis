# 08: Tests, benchmarks, Makefile and CI

---

## `tests/test_framework.h`: a test framework in 80 lines

```cpp
struct TestCase { const char* name; void (*function)(); };
inline std::vector<TestCase>& all_tests() { static std::vector<TestCase> tests; return tests; }
inline int& failure_count() { static int failures = 0; return failures; }
```
The list of tests and the failure counter. They're function-local statics (not globals) to avoid the static-initialization-order problem: tests register themselves *before* `main()` runs, possibly before a global vector would have been constructed.

```cpp
struct TestRegistrar {
    TestRegistrar(const char* name, void (*function)()) { all_tests().push_back({name, function}); }
};
```
**Self-registration trick.** Creating a global `TestRegistrar` object runs its constructor at program start, and the constructor adds the test to the list.

```cpp
#define TEST(name)                                         \
    static void name();                                    \
    static TestRegistrar registrar_##name(#name, name);    \
    static void name()
```
`TEST(foo) { ... }` expands to:
1. a declaration of function `foo`,
2. a static registrar object `registrar_foo("foo", foo)`. `##` glues tokens together, and `#name` turns the name into the string `"foo"`.
3. the start of `foo`'s definition, whose body is the `{ ... }` that follows the macro.

```cpp
#define CHECK(condition) do { if (!(condition)) { std::cerr << ... #condition ...; failure_count()++; } } while (0)
```
Prints the file, line and the condition's source text when it fails. `do { ... } while (0)` makes the macro behave like a single statement (safe inside `if` without braces). A failed CHECK **doesn't stop** the test, so you see every failure at once.

```cpp
#define CHECK_EQ(actual, expected) do { auto actual_value = (actual); auto expected_value = (expected); ... printable(...) ... } while (0)
```
Evaluates each expression **once** (so side effects aren't repeated) and prints both values on failure. `printable` shows `\r\n` visibly, so RESP strings are readable.

## `tests/test_main.cpp`

It runs every registered test, prints PASS/FAIL per test and a summary, and returns 0 or 1. `make` and CI use that exit code to decide success.

## What each test file covers

| File | Main ideas tested |
|---|---|
| `test_resp_parser.cpp` | complete parse; **every prefix of a command is Incomplete** (simulates TCP splitting it at every possible byte); pipelined commands; values containing `\r\n`; inline commands; 5 kinds of malformed input; empty arrays |
| `test_database.cpp` | lazy expiry deletes on access; active expiry respects its limit; `create` clears an old TTL *and* its index entry; rename moves the value and TTL; past expiry deletes; glob matching |
| `test_sorted_set.cpp` | basics; tie-breaking by member; **randomized model test**: 20 000 random add/update/remove operations, comparing range, rank and size against `std::set` every 500 ops |
| `test_commands.cpp` | exact RESP bytes for each command group; WRONGTYPE; arity errors; NX/XX/EX/PX; overflow; negative indexes; empty containers deleted; a ZADD with a bad score changes nothing; regression tests for old bugs (DEL count, SET clearing TTL) |
| `test_aof.cpp` | log + replay restores all 4 types; read-only and no-op commands aren't logged; EX is stored as PEXPIREAT; truncated tail repaired; corrupt file rejected; REWRITEAOF shrinks the file, and writes after it still persist |

**The randomized model test** is the strongest one. Writing a skip list with spans is easy to get subtly wrong, but comparing it with a trivially correct structure over thousands of random operations (with lots of equal scores, to stress the tie-breaking) catches almost any bug. This technique is called **model-based** or **differential testing**.

**The fake clock** (`db.set_clock(fake_clock)`) lets expiry tests run instantly and deterministically. There's no `sleep()` and no flakiness.

## `tests/e2e_test.sh`: end-to-end tests

The real server binary and the real client, talking over real TCP:
- `start_server` launches it in the background (`&`), stores the PID (`$!`), and polls with `PING` until it answers. That's more robust than a fixed `sleep`.
- `check "<expected>" command...` runs the client and compares its output. `$'1) "a"\n2) "b"'` is bash syntax for a string with real newlines.
- **Raw-socket tests** use bash's built-in `/dev/tcp/host/port`. `exec 3<>/dev/tcp/...` opens a TCP connection as file descriptor 3:
  - *Pipelining*: `printf -v payload '...%.0s' $(seq 1 1000)` repeats a command 1000 times (`%.0s` consumes an argument but prints nothing), which is sent in one write. We expect exactly 1000 `PONG`s back.
  - *Split packet*: half a command, a 200 ms pause, then the rest. That proves the input buffer works.
  - *Inline* and *protocol error* cases.
- *Concurrency*: 20 background shell loops each run 25 `INCR`s, and the total must be exactly 500 (no lost updates).
- *Persistence*: SIGTERM (graceful shutdown), restart, then check every type and the TTL survived, and that an already-expired key stayed gone. Then `REWRITEAOF`, check the file shrank, and restart again.
- If the official `redis-cli` is installed, we also check compatibility with it.
- `trap cleanup EXIT` kills the server and deletes temp files even if the script fails.

## `bench/micro_bench.cpp`

Three experiments, each backing a design decision with numbers:

```cpp
template <typename Work>
double time_ms(Work work) {
    auto start = std::chrono::steady_clock::now();
    work();
    auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}
```
Times any lambda. For measuring durations we use `steady_clock` (it never jumps), unlike the wall clock used for TTLs.

1. **vector vs deque push_front**: shows why lists use deque (~1000× faster at 50k elements).
2. **Parser throughput**: 1M pipelined `SET`s parsed in ~25 ms, so parsing is never the bottleneck.
3. **Skip list rank vs std::set**: `std::set` can only compute rank by walking (`std::distance`, O(n)), while the span skip list does it in O(log n). That's ~6000× faster at 200k members.

`long sink` accumulates results so the optimizer can't delete the "unused" work.

## `bench/run_benchmarks.sh`

1. Starts mini-redis with `--no-aof` and runs `redis-benchmark` for 4 scenarios: 1 or 50 clients × no pipelining or pipeline 16. It writes CSV files.
2. If `redis-server` is installed, it starts it with persistence off (a fair comparison) and runs the same scenarios.
3. An `awk` script joins the two CSVs into Markdown tables (`bench/results/summary.md`): requests/s, p50 and p99 latency, and mini/Redis ratio.

Install the tools first: `sudo apt install redis-tools redis-server`.

## `Makefile`

```make
CXXFLAGS := -std=c++17 -Wall -Wextra -g -MMD -MP
```
- `-Wall -Wextra`: lots of warnings (the build has none).
- `-g`: debug symbols, so crashes and sanitizer reports show file and line.
- `-MMD -MP`: the compiler writes `.d` files listing which headers each `.cpp` uses. `-include` at the bottom reads them, so changing a header rebuilds exactly the files that depend on it.

```make
ifeq ($(SAN),1)
  CXXFLAGS += -O1 -fsanitize=address,undefined -fno-omit-frame-pointer
  LDFLAGS  += -fsanitize=address,undefined
  BUILD    := build/san
  BIN      := bin/san
else
  CXXFLAGS += -O2
  BUILD    := build/release
  BIN      := bin
endif
```
`make SAN=1` builds an instrumented copy in separate folders:
- **AddressSanitizer** catches use-after-free, buffer overflows and memory leaks.
- **UBSan** catches undefined behaviour: signed overflow, bad shifts, misaligned pointers, and so on.

```make
CORE_SRCS := $(filter-out server/main.cpp,$(wildcard server/*.cpp))
```
All server code except `main()`. It's linked into the server, the unit tests and the micro benchmarks, so tests exercise exactly the production code.

```make
$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@
```
A **pattern rule**: "to build `build/release/X.o`, compile `X.cpp`". `$<` is the first prerequisite (the .cpp), `$@` is the target (the .o), and `$^` (in link rules) is all prerequisites.

## `.github/workflows/ci.yml`

On every push or PR, GitHub runs two jobs (a **matrix**): a normal build and a sanitizer build. Each builds and runs `make test`. A red ✗ on GitHub means something broke. Once you push this repo, a green CI badge is visible proof the project works.

## `.gitattributes`

`* text=auto eol=lf` forces Unix line endings in the repo. You're on Windows: without this, git could convert scripts to CRLF, and bash fails on `\r` characters.
