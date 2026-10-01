#pragma once

#include <string>

#include "aof.h"

// Settings chosen on the command line (see main.cpp).
struct Config {
    // Like real Redis, only local programs can connect unless told otherwise.
    // There is no password, so opening it to the network is a deliberate choice.
    std::string bind_address = "127.0.0.1";
    int port = 6379;
    bool aof_enabled = true;
    std::string aof_path = "appendonly.aof";
    FsyncPolicy fsync_policy = FsyncPolicy::EverySec;
};
