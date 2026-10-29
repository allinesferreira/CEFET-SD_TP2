#include "protocol.hpp"

#include <arpa/inet.h>

#include <cstring>

namespace {

void append_u32(std::vector<uint8_t>& out, uint32_t value) {
    value = htonl(value);
    auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(value));
}

void append_u16(std::vector<uint8_t>& out, uint16_t value) {
    value = htons(value);
    auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(value));
}

uint32_t read_u32(const uint8_t* data) {
    uint32_t value;
    std::memcpy(&value, data, sizeof(value));
    return ntohl(value);
}

uint16_t read_u16(const uint8_t* data) {
    uint16_t value;
    std::memcpy(&value, data, sizeof(value));
    return ntohs(value);
}

constexpr size_t SENDER_SIZE = sizeof(in_addr) + sizeof(uint16_t);

void append_sender(std::vector<uint8_t>& out, const Address& sender) {
    sockaddr_in addr;
    to_sockaddr(sender, addr);
    auto* ip = reinterpret_cast<const uint8_t*>(&addr.sin_addr);
    out.insert(out.end(), ip, ip + sizeof(in_addr));
    append_u16(out, static_cast<uint16_t>(sender.port));
}

Address read_sender(const uint8_t* data) {
    char host[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, data, host, sizeof(host));
    return {host, read_u16(data + sizeof(in_addr))};
}

size_t request_size(Command command) {
    size_t size = 1 + SENDER_SIZE;
    return command == Command::GetBlock ? size + sizeof(uint32_t) : size;
}

bool is_command(uint8_t byte) {
    return byte == static_cast<uint8_t>(Command::Metadata) || byte == static_cast<uint8_t>(Command::BlockMap) ||
           byte == static_cast<uint8_t>(Command::GetBlock);
}

}

std::vector<uint8_t> encode_request(const Request& request) {
    std::vector<uint8_t> out{static_cast<uint8_t>(request.command)};
    append_sender(out, request.sender);
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
    request.sender = read_sender(&data[1]);
    request.block_index = command == Command::GetBlock ? read_u32(&data[1 + SENDER_SIZE]) : 0;
    return ParseResult::Complete;
}

std::vector<uint8_t> encode_response(const std::vector<uint8_t>& body) {
    std::vector<uint8_t> out;
    out.reserve(sizeof(uint32_t) + body.size());
    append_u32(out, static_cast<uint32_t>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}
