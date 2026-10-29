#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

#include "net.hpp"
#include "protocol.hpp"

using RequestHandler = std::function<std::vector<uint8_t>(const Request&)>;

void serve(const Address& address, const std::atomic<bool>& running, const RequestHandler& handler);
