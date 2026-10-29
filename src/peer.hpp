#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "metadata.hpp"
#include "net.hpp"
#include "protocol.hpp"

extern std::atomic<bool> running;

class Peer {
public:
    Peer(Address self, std::string file, std::string dir, uint32_t block_size,
         std::vector<Address> neighbors, int linger_seconds);

    void run();

private:
    void init();
    std::vector<uint8_t> respond(const Request& request);
    std::vector<uint8_t> read_block(size_t index);

    bool query(const Address& neighbor, const Request& request, std::vector<uint8_t>& body, uint32_t max_size);
    bool fetch_metadata();
    void worker(Address neighbor);
    bool fetch_block_map(const Address& neighbor, std::vector<uint8_t>& remote);
    bool fetch_block(const Address& neighbor, size_t index, std::vector<uint8_t>& data);
    bool write_block(size_t index, const std::vector<uint8_t>& data);
    void finish_download();

    Metadata meta;
    std::string source_path;
    std::string local_path;
    std::string dir;
    int local_fd = -1;
    Address self;
    bool seed = false;
    int linger;
    std::vector<Address> neighbors;

    std::vector<uint8_t> have;
    std::vector<uint8_t> claimed;
    std::mutex blocks_mutex;

    std::atomic<size_t> completed{0};
    std::atomic<bool> finished{false};
    std::atomic<bool> ready{false};
    std::chrono::steady_clock::time_point start;
};
