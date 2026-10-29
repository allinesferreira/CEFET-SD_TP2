#include "net.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

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

int connect_to(const std::string& host, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    set_timeouts(fd, 3);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1 ||
        connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}
