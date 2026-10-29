#include "log.hpp"

#include <iostream>
#include <mutex>

namespace {
std::mutex log_mutex;
}

void log_line(const std::string& text) {
    std::lock_guard<std::mutex> lock(log_mutex);
    std::cerr << text << '\n';
}
