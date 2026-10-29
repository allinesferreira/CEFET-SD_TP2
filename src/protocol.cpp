#include "protocol.hpp"

#include <arpa/inet.h>

#include <cstring>

namespace {

void append_u32(std::vector<uint8_t>& out, uint32_t value) {
    value = htonl(value);
    auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(value));
}

uint32_t read_u32(const uint8_t* data) {
    uint32_t value;
    std::memcpy(&value, data, sizeof(value));
    return ntohl(value);
}

size_t request_size(Command command) {
    return command == Command::GetBlock ? 1 + sizeof(uint32_t) : 1;
}

bool is_command(uint8_t byte) {
    return byte == static_cast<uint8_t>(Command::Metadata) || byte == static_cast<uint8_t>(Command::BlockMap) ||
           byte == static_cast<uint8_t>(Command::GetBlock);
}

}

std::vector<uint8_t> encode_request(const Request& request) {
    std::vector<uint8_t> out{static_cast<uint8_t>(request.command)};
    if (request.command == Command::GetBlock)
        append_u32(out, request.block_index);
    return out;
}

ParseResult parse_request(const std::vector<uint8_t>& data, Request& request) {
    if (data.empty())
        return ParseResult::Incomplete;
    if (!is_command(data[0]))
        return ParseResult::Invalid;

    auto command = static_cast<Command>(data[0]);
    size_t size = request_size(command);
    if (data.size() < size)
        return ParseResult::Incomplete;
    if (data.size() > size)
        return ParseResult::Invalid;

    request.command = command;
    request.block_index = command == Command::GetBlock ? read_u32(&data[1]) : 0;
    return ParseResult::Complete;
}

std::vector<uint8_t> encode_response(const std::vector<uint8_t>& body) {
    std::vector<uint8_t> out;
    out.reserve(sizeof(uint32_t) + body.size());
    append_u32(out, static_cast<uint32_t>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}
