#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "metadata.hpp"

extern std::atomic<bool> running;

struct Neighbor {
    std::string host;
    int port;

    std::string str() const { return host + ":" + std::to_string(port); }
};

class Peer {
public:
    Peer(int port, std::string file, std::string dir, uint32_t block_size,
         std::vector<Neighbor> neighbors, int linger_seconds);

    void run();

private:
    void init();
    bool fetch_metadata();
    void server();
    void handle(int fd);
    void worker(Neighbor neighbor);
    bool fetch_block_map(const Neighbor& neighbor, std::vector<uint8_t>& remote);
    bool fetch_block(const Neighbor& neighbor, size_t index, std::vector<uint8_t>& data);
    bool write_block(size_t index, const std::vector<uint8_t>& data);
    void finish_download();

    Metadata meta;
    std::string source_path;
    std::string local_path;
    std::string dir;
    int local_fd = -1;
    int port;
    bool seed = false;
    int linger;
    std::vector<Neighbor> neighbors;

    std::vector<uint8_t> have;
    std::vector<uint8_t> claimed;
    std::mutex blocks_mutex;

    std::atomic<size_t> completed{0};
    std::atomic<bool> finished{false};
    std::atomic<bool> ready{false};
    std::chrono::steady_clock::time_point start;
};
