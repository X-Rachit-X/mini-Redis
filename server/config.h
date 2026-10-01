#pragma once

#include <string>

#include "aof.h"

// Settings chosen on the command line (see main.cpp).
struct Config {
    int port = 6379;
    bool aof_enabled = true;
    std::string aof_path = "appendonly.aof";
    FsyncPolicy fsync_policy = FsyncPolicy::EverySec;
};
