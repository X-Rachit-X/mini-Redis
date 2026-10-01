#pragma once

#include <string>

#include "reply.h"

// Formats a reply the same way redis-cli does, e.g.
//   OK                (status)
//   "hello"           (bulk string, quoted)
//   (integer) 42
//   (nil)
//   (error) ERR ...
//   1) "a"            (arrays: numbered, nested arrays indented)
//   2) "b"
std::string format_reply(const Reply& reply, const std::string& indent = "");
