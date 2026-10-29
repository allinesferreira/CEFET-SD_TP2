#pragma once

#include <cstdint>
#include <vector>

#include "net.hpp"

enum class Command : uint8_t {
    Metadata = 'M',
    BlockMap = 'H',
    GetBlock = 'G',
};

struct Request {
    Command command;
    Address sender;
    uint32_t block_index = 0;
};

enum class ParseResult { Incomplete, Invalid, Complete };

std::vector<uint8_t> encode_request(const Request& request);
ParseResult parse_request(const std::vector<uint8_t>& data, Request& request);

std::vector<uint8_t> encode_response(const std::vector<uint8_t>& body);
