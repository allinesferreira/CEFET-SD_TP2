#include "metadata.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "hash.hpp"

namespace fs = std::filesystem;

namespace {

bool is_plain_filename(const std::string& name) {
    return !name.empty() && name != "." && name != ".." && name.find('/') == std::string::npos &&
           name.find('\\') == std::string::npos;
}

}

size_t Metadata::block_count() const {
    return (size + block_size - 1) / block_size;
}

size_t Metadata::block_length(size_t index) const {
    return static_cast<size_t>(std::min<uint64_t>(block_size, size - static_cast<uint64_t>(index) * block_size));
}

std::string Metadata::serialize() const {
    std::ostringstream out;
    out << "P2PMETA1\n" << name << '\n' << size << '\n' << block_size << '\n' << sha256 << '\n';
    for (const auto& hash : block_sha256)
        out << hash << '\n';
    return out.str();
}

Metadata Metadata::parse(const std::string& text) {
    std::istringstream in(text);
    Metadata m;
    std::string line;

    if (!std::getline(in, line) || line != "P2PMETA1" || !std::getline(in, m.name))
        throw std::runtime_error("metadados invalidos");
    if (!std::getline(in, line))
        throw std::runtime_error("tamanho ausente");
    m.size = std::stoull(line);
    if (!std::getline(in, line))
        throw std::runtime_error("bloco ausente");
    m.block_size = static_cast<uint32_t>(std::stoul(line));
    if (!std::getline(in, m.sha256))
        throw std::runtime_error("hash ausente");

    if (!is_plain_filename(m.name) || m.size == 0 || m.block_size < MIN_BLOCK_SIZE || m.block_size > MAX_BLOCK_SIZE ||
        m.block_count() > MAX_BLOCKS || m.sha256.size() != 64)
        throw std::runtime_error("metadados fora dos limites");

    for (size_t i = 0; i < m.block_count(); i++) {
        if (!std::getline(in, line) || line.size() != 64)
            throw std::runtime_error("hash de bloco ausente");
        m.block_sha256.push_back(line);
    }
    return m;
}

Metadata Metadata::from_file(const std::string& path, uint32_t block_size) {
    Metadata m;
    m.name = fs::path(path).filename().string();
    m.size = fs::file_size(path);
    m.block_size = block_size;
    if (m.size == 0 || m.block_count() > MAX_BLOCKS)
        throw std::runtime_error("arquivo vazio ou grande demais");

    m.sha256 = sha256_file(path);
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> buffer(block_size);
    for (size_t i = 0; i < m.block_count(); i++) {
        size_t n = m.block_length(i);
        file.read(reinterpret_cast<char*>(buffer.data()), n);
        if (static_cast<size_t>(file.gcount()) != n)
            throw std::runtime_error("erro ao ler bloco");
        m.block_sha256.push_back(sha256_hex(buffer.data(), n));
    }
    return m;
}
