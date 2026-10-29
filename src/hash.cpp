#include "hash.hpp"

#include <openssl/sha.h>

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {

std::string to_hex(const unsigned char* bytes, size_t n) {
    std::ostringstream out;
    for (size_t i = 0; i < n; i++)
        out << std::hex << std::setfill('0') << std::setw(2) << static_cast<unsigned>(bytes[i]);
    return out.str();
}

}

std::string sha256_hex(const uint8_t* data, size_t n) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(data, n, hash);
    return to_hex(hash, sizeof(hash));
}

std::string sha256_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("nao abriu arquivo para SHA256");

    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    std::vector<char> buffer(65536);
    while (file) {
        file.read(buffer.data(), buffer.size());
        std::streamsize n = file.gcount();
        if (n > 0)
            SHA256_Update(&ctx, buffer.data(), n);
    }
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &ctx);
    return to_hex(hash, sizeof(hash));
}
