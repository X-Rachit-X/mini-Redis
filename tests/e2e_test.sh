#!/usr/bin/env bash
# End-to-end tests: start the real server binary, talk to it over TCP with
# the real client (and raw sockets), and check what comes back.
#
# Usage: BIN_DIR=bin bash tests/e2e_test.sh

set -u
BIN_DIR="${BIN_DIR:-bin}"
SERVER="$BIN_DIR/mini-redis-server"
CLI="$BIN_DIR/mini-redis-cli"
PORT="${PORT:-7379}"
WORK_DIR="$(mktemp -d)"
AOF="$WORK_DIR/test.aof"
PASSED=0
FAILED=0
SERVER_PID=""

cleanup() {
    if [ -n "$SERVER_PID" ]; then kill "$SERVER_PID" 2>/dev/null; fi
    rm -rf "$WORK_DIR"
}
trap cleanup EXIT

start_server() {
    "$SERVER" --port "$PORT" --aof-file "$AOF" >> "$WORK_DIR/server.log" 2>&1 &
    SERVER_PID=$!
    for _ in $(seq 1 50); do  # wait up to 5 seconds for it to accept connections
        if "$CLI" -p "$PORT" PING > /dev/null 2>&1; then return 0; fi
        sleep 0.1
    done
    echo "Server did not start:"; cat "$WORK_DIR/server.log"; exit 1
}

stop_server() {
    kill -TERM "$SERVER_PID"  # graceful shutdown (flushes the AOF)
    wait "$SERVER_PID"
    SERVER_PID=""
}

pass() { PASSED=$((PASSED + 1)); }
fail() {
    FAILED=$((FAILED + 1))
    echo "FAIL: $1"
    echo "  expected: $2"
    echo "  actual:   $3"
}

# check "<expected output>" command args...
check() {
    local expected="$1"
    shift
    local actual
    actual="$("$CLI" -p "$PORT" "$@" 2>&1)"
    if [ "$actual" == "$expected" ]; then pass; else fail "$*" "$expected" "$actual"; fi
}

echo "== basic commands through mini-redis-cli"
start_server
check "PONG" PING
check "OK" SET greeting "hello world"
check '"hello world"' GET greeting
check "(nil)" GET missing
check "(integer) 3" RPUSH letters a b c
check $'1) "a"\n2) "b"\n3) "c"' LRANGE letters 0 -1
check "(error) WRONGTYPE Operation against a key holding the wrong kind of value" GET letters
check "(integer) 2" HSET user:1 name Alice age 30
check '"Alice"' HGET user:1 name
check "(integer) 3" ZADD board 30 carol 10 alice 20 bob
check $'1) "alice"\n2) "10"\n3) "bob"\n4) "20"' ZRANGE board 0 1 WITHSCORES
check "(integer) 2" ZRANK board carol
check "(error) ERR unknown command 'NOPE'" NOPE

echo "== commands piped into the REPL"
actual="$(printf 'SET piped "a b"\nGET piped\nINCR hits\nINCR hits\n' | "$CLI" -p "$PORT")"
expected=$'OK\n"a b"\n(integer) 1\n(integer) 2'
if [ "$actual" == "$expected" ]; then pass; else fail "piped REPL" "$expected" "$actual"; fi

echo "== key expiry"
check "OK" SET shortlived x PX 200
check '"x"' GET shortlived
sleep 0.4
check "(nil)" GET shortlived

echo "== pipelining: 1000 commands in a single write"
exec 3<>"/dev/tcp/127.0.0.1/$PORT"
printf -v payload '*1\r\n$4\r\nPING\r\n%.0s' $(seq 1 1000)  # repeat the command 1000 times
printf '%s' "$payload" >&3
replies="$(timeout 5 head -c 7000 <&3 | grep -c PONG)"  # 1000 x "+PONG\r\n" = 7000 bytes
exec 3>&-
if [ "$replies" == "1000" ]; then pass; else fail "pipelining" "1000" "$replies"; fi

echo "== a command split across two TCP packets"
exec 3<>"/dev/tcp/127.0.0.1/$PORT"
printf '*2\r\n$4\r\nECHO\r\n$5\r\nhel' >&3
sleep 0.2
printf 'lo\r\n' >&3
reply="$(timeout 2 head -c 11 <&3 | tr -d '\r\n')"
exec 3>&-
if [ "$reply" == "\$5hello" ]; then pass; else fail "split packet" "\$5hello" "$reply"; fi

echo "== inline command (what you'd type in telnet)"
exec 3<>"/dev/tcp/127.0.0.1/$PORT"
printf 'GET greeting\r\n' >&3
reply="$(timeout 2 head -c 18 <&3 | tr -d '\r\n')"
exec 3>&-
if [ "$reply" == "\$11hello world" ]; then pass; else fail "inline" "\$11hello world" "$reply"; fi

echo "== protocol error closes the connection"
exec 3<>"/dev/tcp/127.0.0.1/$PORT"
printf '*1\r\n$x\r\n' >&3
reply="$(timeout 2 cat <&3)"
exec 3>&-
if [[ "$reply" == -ERR\ Protocol\ error* ]]; then pass; else fail "protocol error" "-ERR Protocol error..." "$reply"; fi

echo "== 20 concurrent clients x 25 INCR"
for _ in $(seq 1 20); do
    (for _ in $(seq 1 25); do "$CLI" -p "$PORT" INCR concurrent > /dev/null; done) &
done
wait $(jobs -p | grep -v "^$SERVER_PID\$")
check '"500"' GET concurrent

echo "== data survives a restart (AOF)"
check "OK" SET with_ttl v EX 1000
stop_server
start_server
check '"hello world"' GET greeting
check $'1) "a"\n2) "b"\n3) "c"' LRANGE letters 0 -1
check '"Alice"' HGET user:1 name
check "(integer) 2" ZRANK board carol
check '"500"' GET concurrent
ttl="$("$CLI" -p "$PORT" TTL with_ttl)"
if [[ "$ttl" == "(integer) 1000" || "$ttl" == "(integer) 999" ]]; then pass; else fail "TTL after restart" "(integer) 1000" "$ttl"; fi
check "(nil)" GET shortlived  # expired before the restart, must stay gone

echo "== REWRITEAOF compacts the file and keeps the data"
size_before=$(stat -c %s "$AOF")
check "OK" REWRITEAOF
size_after=$(stat -c %s "$AOF")
if [ "$size_after" -lt "$size_before" ]; then pass; else fail "AOF smaller" "< $size_before" "$size_after"; fi
stop_server
start_server
check '"500"' GET concurrent
check '"Alice"' HGET user:1 name

if command -v redis-cli > /dev/null; then
    echo "== compatibility with the official redis-cli"
    actual="$(redis-cli -p "$PORT" SET official yes) $(redis-cli -p "$PORT" GET official)"
    if [ "$actual" == "OK yes" ]; then pass; else fail "redis-cli" "OK yes" "$actual"; fi
fi

stop_server
echo
echo "$PASSED passed, $FAILED failed"
[ "$FAILED" -eq 0 ]
