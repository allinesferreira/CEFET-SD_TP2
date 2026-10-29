#include <csignal>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "metadata.hpp"
#include "peer.hpp"

namespace {

const char* USAGE =
    "uso: peer --port PORT [--file CAMINHO] [--neighbor IP:PORT ...] [--dir PASTA] [--block BYTES] "
    "[--linger SEGUNDOS]";

void stop(int) {
    running = false;
}

Neighbor parse_neighbor(const std::string& value) {
    size_t pos = value.rfind(':');
    if (pos == std::string::npos)
        throw std::runtime_error("vizinho deve ser IP:porta");
    int port = std::stoi(value.substr(pos + 1));
    if (port < 1 || port > 65535)
        throw std::runtime_error("porta vizinho invalida");
    return {value.substr(0, pos), port};
}

}

int main(int argc, char** argv) {
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    std::signal(SIGPIPE, SIG_IGN);

    try {
        int port = 0;
        std::string file;
        std::string dir = "downloads";
        uint32_t block_size = 1024;
        int linger = 30;
        std::vector<Neighbor> neighbors;

        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            auto value = [&]() {
                if (i + 1 >= argc)
                    throw std::runtime_error("falta valor para " + arg);
                return std::string(argv[++i]);
            };

            if (arg == "--port")
                port = std::stoi(value());
            else if (arg == "--file")
                file = value();
            else if (arg == "--dir")
                dir = value();
            else if (arg == "--block")
                block_size = static_cast<uint32_t>(std::stoul(value()));
            else if (arg == "--linger")
                linger = std::stoi(value());
            else if (arg == "--neighbor")
                neighbors.push_back(parse_neighbor(value()));
            else
                throw std::runtime_error("argumento desconhecido: " + arg);
        }

        if (port < 1 || port > 65535 || block_size < MIN_BLOCK_SIZE || block_size > MAX_BLOCK_SIZE ||
            (file.empty() && neighbors.empty()))
            throw std::runtime_error(USAGE);
        if (linger < 0 || linger > 3600)
            throw std::runtime_error("linger invalido");

        Peer peer(port, file, dir, block_size, neighbors, linger);
        peer.run();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERRO: " << e.what() << '\n';
        return 1;
    }
}
