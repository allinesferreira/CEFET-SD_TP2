#include "log.hpp"

#include <chrono>
#include <cstdio>
#include <iostream>
#include <mutex>

namespace {

std::mutex log_mutex;
const auto process_start = std::chrono::steady_clock::now();

std::string elapsed_prefix() {
    std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - process_start;
    char prefix[32];
    std::snprintf(prefix, sizeof(prefix), "[%8.3f] ", elapsed.count());
    return prefix;
}

}

void log_line(const std::string& text) {
    std::string line = elapsed_prefix() + text;
    std::lock_guard<std::mutex> lock(log_mutex);
    std::cerr << line << '\n';
}
