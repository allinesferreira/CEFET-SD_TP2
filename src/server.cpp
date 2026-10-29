#include "server.hpp"

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <stdexcept>

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto IDLE_TIMEOUT = std::chrono::seconds(5);
constexpr int POLL_INTERVAL_MS = 200;

struct Connection {
    explicit Connection(int fd) : fd(fd) {}

    int fd;
    std::vector<uint8_t> input;
    std::vector<uint8_t> output;
    size_t sent = 0;
    bool responding = false;
    Clock::time_point last_activity = Clock::now();
};

bool would_block() {
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

void accept_pending(int listener, std::vector<Connection>& connections) {
    while (true) {
        int fd = accept4(listener, nullptr, nullptr, SOCK_NONBLOCK);
        if (fd < 0)
            return;
        connections.emplace_back(fd);
    }
}

bool read_request(Connection& c, const RequestHandler& handler) {
    uint8_t buffer[4096];
    while (true) {
        ssize_t n = recv(c.fd, buffer, sizeof(buffer), 0);
        if (n > 0) {
            c.input.insert(c.input.end(), buffer, buffer + n);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && would_block())
            break;
        return false;
    }

    Request request;
    switch (parse_request(c.input, request)) {
    case ParseResult::Incomplete:
        return true;
    case ParseResult::Invalid:
        return false;
    case ParseResult::Complete:
        c.output = encode_response(handler(request));
        c.responding = true;
        return true;
    }
    return false;
}

bool write_pending(Connection& c) {
    while (c.sent < c.output.size()) {
        ssize_t n = send(c.fd, c.output.data() + c.sent, c.output.size() - c.sent, MSG_NOSIGNAL);
        if (n > 0) {
            c.sent += n;
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        return n < 0 && would_block();
    }
    return false;
}

bool keep_open(Connection& c, short events, const RequestHandler& handler) {
    auto now = Clock::now();
    if (events == 0)
        return now - c.last_activity < IDLE_TIMEOUT;
    c.last_activity = now;

    if (!c.responding && (events & POLLIN) && !read_request(c, handler))
        return false;
    if (c.responding)
        return write_pending(c);
    return !(events & (POLLERR | POLLHUP | POLLNVAL));
}

}

int listen_on(const Address& address) {
    sockaddr_in addr;
    if (!to_sockaddr(address, addr))
        throw std::runtime_error("endereco invalido " + address.str());

    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd < 0)
        throw std::runtime_error("socket servidor");
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 || listen(fd, 128) < 0) {
        close(fd);
        throw std::runtime_error("bind/listen " + address.str());
    }
    return fd;
}

void serve(int listener, const std::atomic<bool>& running, const RequestHandler& handler) {
    std::vector<Connection> connections;
    std::vector<pollfd> fds;
    while (running) {
        fds.clear();
        fds.push_back({listener, POLLIN, 0});
        for (const auto& c : connections)
            fds.push_back({c.fd, static_cast<short>(c.responding ? POLLOUT : POLLIN), 0});

        if (poll(fds.data(), fds.size(), POLL_INTERVAL_MS) < 0)
            continue;

        for (size_t i = 0; i < connections.size(); i++) {
            if (!keep_open(connections[i], fds[i + 1].revents, handler)) {
                close(connections[i].fd);
                connections[i].fd = -1;
            }
        }
        connections.erase(std::remove_if(connections.begin(), connections.end(),
                                         [](const Connection& c) { return c.fd < 0; }),
                          connections.end());

        if (fds[0].revents & POLLIN)
            accept_pending(listener, connections);
    }

    for (const auto& c : connections)
        close(c.fd);
    close(listener);
}
