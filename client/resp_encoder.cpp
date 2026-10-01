#include "resp_encoder.h"

std::string encode_command(const std::vector<std::string>& args) {
    std::string out = "*" + std::to_string(args.size()) + "\r\n";
    for (const std::string& arg : args) {
        out += "$" + std::to_string(arg.size()) + "\r\n";
        out += arg;
        out += "\r\n";
    }
    return out;
}
