#!/usr/bin/env bash
# Benchmarks mini-redis with the official redis-benchmark tool and, if it is
# installed, compares it with the real Redis server under the same settings.
#
# Needs:  sudo apt install redis-tools          (redis-benchmark)
# Optional: sudo apt install redis-server       (for the comparison)
#
# Usage:  BIN_DIR=bin bash bench/run_benchmarks.sh
# Output: bench/results/summary.md (+ raw CSV files)

set -eu
BIN_DIR="${BIN_DIR:-bin}"
REQUESTS="${REQUESTS:-200000}"
TESTS="set,get,incr,lpush,rpop,hset,zadd"
OUT_DIR="bench/results"
MINI_PORT=7001
REDIS_PORT=7002

if ! command -v redis-benchmark > /dev/null; then
    echo "redis-benchmark not found. Install it with: sudo apt install redis-tools"
    exit 1
fi
mkdir -p "$OUT_DIR"
rm -f "$OUT_DIR"/*.csv

wait_for_port() {
    for _ in $(seq 1 50); do
        # redis-cli fails fast while the port is closed. (redis-benchmark does
        # not: it spins forever on a refused connection, hanging this loop.)
        if redis-cli -p "$1" PING > /dev/null 2>&1; then return 0; fi
        sleep 0.1
    done
    echo "server on port $1 did not start"; exit 1
}

# run_suite <name> <port>: runs every scenario against one server.
run_suite() {
    local name="$1" port="$2"
    for clients in 1 50; do
        for pipeline in 1 16; do
            echo "  $name: $clients client(s), pipeline $pipeline"
            redis-benchmark -p "$port" -n "$REQUESTS" -c "$clients" -P "$pipeline" \
                -t "$TESTS" --csv > "$OUT_DIR/${name}_c${clients}_P${pipeline}.csv"
        done
    done
}

echo "Benchmarking mini-redis (AOF off)..."
"$BIN_DIR/mini-redis-server" --port "$MINI_PORT" --no-aof > /dev/null &
MINI_PID=$!
trap 'kill $MINI_PID 2>/dev/null || true' EXIT
wait_for_port "$MINI_PORT"
run_suite mini "$MINI_PORT"
kill "$MINI_PID"; wait "$MINI_PID" 2>/dev/null || true

if command -v redis-server > /dev/null; then
    echo "Benchmarking real Redis (persistence off, same as mini-redis)..."
    redis-server --port "$REDIS_PORT" --save "" --appendonly no > /dev/null &
    REDIS_PID=$!
    trap 'kill $REDIS_PID 2>/dev/null || true' EXIT
    wait_for_port "$REDIS_PORT"
    run_suite redis "$REDIS_PORT"
    kill "$REDIS_PID"; wait "$REDIS_PID" 2>/dev/null || true
else
    echo "redis-server not installed: skipping the comparison"
fi

# ---- Build a Markdown summary --------------------------------------------
SUMMARY="$OUT_DIR/summary.md"
{
    echo "# Benchmark results"
    echo
    echo "- Machine: $(nproc) CPUs, $(uname -sr)"
    echo "- Requests per test: $REQUESTS, run with redis-benchmark --csv"
    echo "- Both servers single-threaded with persistence disabled"
    echo
    for clients in 1 50; do
        for pipeline in 1 16; do
            mini="$OUT_DIR/mini_c${clients}_P${pipeline}.csv"
            redis="$OUT_DIR/redis_c${clients}_P${pipeline}.csv"
            [ -f "$redis" ] || redis=/dev/null
            echo "## $clients client(s), pipeline $pipeline"
            echo
            echo "| Test | mini-redis req/s | p50 ms | p99 ms | Redis req/s | p50 ms | p99 ms | mini / Redis |"
            echo "|---|---:|---:|---:|---:|---:|---:|---:|"
            # CSV columns: test, rps, avg, min, p50, p95, p99, max (all quoted)
            awk -F, -v redis_file="$redis" '
                { gsub(/"/, "") }
                $1 == "test" { next }
                FILENAME == redis_file { r_rps[$1] = $2; r_p50[$1] = $5; r_p99[$1] = $7; next }
                {
                    if ($1 in r_rps) ratio = sprintf("%.0f%%", 100 * $2 / r_rps[$1])
                    else { ratio = "-"; r_rps[$1] = "-"; r_p50[$1] = "-"; r_p99[$1] = "-" }
                    printf "| %s | %s | %s | %s | %s | %s | %s | %s |\n",
                        $1, $2, $5, $7, r_rps[$1], r_p50[$1], r_p99[$1], ratio
                }' "$redis" "$mini"
            echo
        done
    done
} > "$SUMMARY"

echo
cat "$SUMMARY"
