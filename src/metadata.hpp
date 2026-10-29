#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

constexpr uint32_t MIN_BLOCK_SIZE = 256;
constexpr uint32_t MAX_BLOCK_SIZE = 1048576;
constexpr size_t MAX_BLOCKS = 1000000;

struct Metadata {
    std::string name;
    uint64_t size = 0;
    uint32_t block_size = 1024;
    std::string sha256;
    std::vector<std::string> block_sha256;

    size_t block_count() const;
    size_t block_length(size_t index) const;

    std::string serialize() const;
    static Metadata parse(const std::string& text);

    static Metadata from_file(const std::string& path, uint32_t block_size);
};
