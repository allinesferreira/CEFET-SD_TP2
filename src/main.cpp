#include <csignal>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "metadata.hpp"
#include "net.hpp"
#include "peer.hpp"

namespace {

const char* USAGE =
    "uso: peer --ip IP --port PORT [--file CAMINHO] [--neighbor IP:PORT ...] [--dir PASTA] [--block BYTES] "
    "[--linger SEGUNDOS]";

void stop(int) {
    running = false;
}

int parse_int(const std::string& option, const std::string& value) {
    size_t used = 0;
    int result = 0;
    try {
        result = std::stoi(value, &used);
    } catch (const std::exception&) {
        used = 0;
    }
    if (used == 0 || used != value.size())
        throw std::runtime_error("valor invalido para " + option + ": " + value);
    return result;
}

Address parse_neighbor(const std::string& value) {
    size_t pos = value.rfind(':');
    if (pos == std::string::npos)
        throw std::runtime_error("vizinho deve ser IP:porta");
    int port = parse_int("--neighbor", value.substr(pos + 1));
    if (port < 1 || port > 65535)
        throw std::runtime_error("porta vizinho invalida");
    std::string host = value.substr(0, pos);
    if (!is_ipv4(host))
        throw std::runtime_error("ip vizinho invalido: " + host);
    return {host, port};
}

}

int main(int argc, char** argv) {
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    std::signal(SIGPIPE, SIG_IGN);

    try {
        std::string ip;
        int port = 0;
        std::string file;
        std::string dir;
        int block_size = 1024;
        int linger = 30;
        std::vector<Address> neighbors;

        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            auto value = [&]() {
                if (i + 1 >= argc)
                    throw std::runtime_error("falta valor para " + arg);
                return std::string(argv[++i]);
            };

            if (arg == "--ip")
                ip = value();
            else if (arg == "--port")
                port = parse_int(arg, value());
            else if (arg == "--file")
                file = value();
            else if (arg == "--dir")
                dir = value();
            else if (arg == "--block")
                block_size = parse_int(arg, value());
            else if (arg == "--linger")
                linger = parse_int(arg, value());
            else if (arg == "--neighbor")
                neighbors.push_back(parse_neighbor(value()));
            else
                throw std::runtime_error("argumento desconhecido: " + arg);
        }

        if (ip.empty() || port < 1 || port > 65535 || block_size < static_cast<int>(MIN_BLOCK_SIZE) ||
            block_size > static_cast<int>(MAX_BLOCK_SIZE) ||
            (file.empty() && neighbors.empty()))
            throw std::runtime_error(USAGE);
        if (!is_ipv4(ip))
            throw std::runtime_error("ip invalido: " + ip);
        if (linger < 0 || linger > 3600)
            throw std::runtime_error("linger invalido");
        if (!file.empty() && !std::filesystem::is_regular_file(file))
            throw std::runtime_error("arquivo nao encontrado: " + file);

        if (dir.empty())
            dir = "downloads/" + std::to_string(port);

        Peer peer({ip, port}, file, dir, static_cast<uint32_t>(block_size), neighbors, linger);
        peer.run();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERRO: " << e.what() << '\n';
        return 1;
    }
}
