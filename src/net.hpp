#pragma once

#include <netinet/in.h>

#include <cstddef>
#include <cstdint>
#include <string>

struct Address {
    std::string host;
    int port;

    std::string str() const { return host + ":" + std::to_string(port); }
};

bool to_sockaddr(const Address& address, sockaddr_in& result);
bool is_ipv4(const std::string& host);

bool send_all(int fd, const void* data, size_t n);
bool recv_all(int fd, void* data, size_t n);

bool send_u32(int fd, uint32_t value);
bool recv_u32(int fd, uint32_t& value);

void set_timeouts(int fd, int seconds);

int connect_to(const Address& address);
