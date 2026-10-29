#include "net.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

bool to_sockaddr(const Address& address, sockaddr_in& result) {
    result = {};
    result.sin_family = AF_INET;
    result.sin_port = htons(address.port);
    return inet_pton(AF_INET, address.host.c_str(), &result.sin_addr) == 1;
}

bool is_ipv4(const std::string& host) {
    sockaddr_in addr;
    return to_sockaddr({host, 0}, addr);
}

bool send_all(int fd, const void* data, size_t n) {
    auto* p = static_cast<const uint8_t*>(data);
    while (n > 0) {
        ssize_t sent = send(fd, p, n, MSG_NOSIGNAL);
        if (sent <= 0)
            return false;
        p += sent;
        n -= sent;
    }
    return true;
}

bool recv_all(int fd, void* data, size_t n) {
    auto* p = static_cast<uint8_t*>(data);
    while (n > 0) {
        ssize_t received = recv(fd, p, n, 0);
        if (received <= 0)
            return false;
        p += received;
        n -= received;
    }
    return true;
}

bool send_u32(int fd, uint32_t value) {
    value = htonl(value);
    return send_all(fd, &value, sizeof(value));
}

bool recv_u32(int fd, uint32_t& value) {
    if (!recv_all(fd, &value, sizeof(value)))
        return false;
    value = ntohl(value);
    return true;
}

void set_timeouts(int fd, int seconds) {
    timeval tv{seconds, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

int connect_to(const Address& address) {
    sockaddr_in addr;
    if (!to_sockaddr(address, addr))
        return -1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    set_timeouts(fd, 3);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}
