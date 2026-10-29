#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

bool send_all(int fd, const void* data, size_t n);
bool recv_all(int fd, void* data, size_t n);

bool send_u32(int fd, uint32_t value);
bool recv_u32(int fd, uint32_t& value);

void set_timeouts(int fd, int seconds);

int connect_to(const std::string& host, int port);
