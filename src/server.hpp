#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

#include "net.hpp"
#include "protocol.hpp"

using RequestHandler = std::function<std::vector<uint8_t>(const Request&)>;

int listen_on(const Address& address);
void serve(int listener, const std::atomic<bool>& running, const RequestHandler& handler);
