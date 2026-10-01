#pragma once

#include <cstdint>

// Current wall-clock time in milliseconds since the Unix epoch (1 Jan 1970).
//
// We use absolute wall-clock time (not a steady/monotonic clock) because
// expiry times are written to the AOF file and must still mean the same
// moment after the server restarts.
int64_t now_ms();
