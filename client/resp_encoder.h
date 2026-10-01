#pragma once

#include <string>
#include <vector>

// Turns {"SET", "name", "Alice"} into the RESP bytes the server expects:
//   *3\r\n$3\r\nSET\r\n$4\r\nname\r\n$5\r\nAlice\r\n
std::string encode_command(const std::vector<std::string>& args);
