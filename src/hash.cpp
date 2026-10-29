#include "hash.hpp"

#include <openssl/evp.h>

#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {

using DigestContext = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;

std::string to_hex(const unsigned char* bytes, size_t n) {
    std::ostringstream out;
    for (size_t i = 0; i < n; i++)
        out << std::hex << std::setfill('0') << std::setw(2) << static_cast<unsigned>(bytes[i]);
    return out.str();
}

}

std::string sha256_hex(const uint8_t* data, size_t n) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    if (!EVP_Digest(data, n, hash, &length, EVP_sha256(), nullptr))
        throw std::runtime_error("falha ao calcular SHA256");
    return to_hex(hash, length);
}

std::string sha256_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("nao abriu arquivo para SHA256");

    DigestContext ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!ctx || !EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr))
        throw std::runtime_error("falha ao calcular SHA256");

    std::vector<char> buffer(65536);
    while (file) {
        file.read(buffer.data(), buffer.size());
        std::streamsize n = file.gcount();
        if (n > 0)
            EVP_DigestUpdate(ctx.get(), buffer.data(), n);
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    EVP_DigestFinal_ex(ctx.get(), hash, &length);
    return to_hex(hash, length);
}
