#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

std::string sha256_hex(const uint8_t* data, size_t n);
std::string sha256_file(const std::string& path);
