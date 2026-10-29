#include "peer.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>

#include "hash.hpp"
#include "log.hpp"
#include "server.hpp"

namespace fs = std::filesystem;
using namespace std::chrono_literals;

std::atomic<bool> running{true};

namespace {

constexpr uint32_t MAX_METADATA_SIZE = 70000000;

}

Peer::Peer(Address self, std::string file, std::string dir, uint32_t block_size,
           std::vector<Address> neighbors, int linger_seconds)
    : source_path(std::move(file)),
      dir(std::move(dir)),
      self(std::move(self)),
      linger(linger_seconds),
      neighbors(std::move(neighbors)) {
    seed = !source_path.empty();
    meta.block_size = block_size;
}

void Peer::run() {
    std::thread server_thread([this] {
        try {
            serve(self, running, [this](const Request& request) { return respond(request); });
        } catch (const std::exception& e) {
            log_line(e.what());
            running = false;
        }
    });

    try {
        init();
        start = std::chrono::steady_clock::now();
        std::vector<std::thread> workers;
        if (!seed) {
            for (const auto& n : neighbors)
                workers.emplace_back([this, n] { worker(n); });
            while (running && !finished)
                std::this_thread::sleep_for(100ms);
            if (finished)
                finish_download();
            running = false;
        } else {
            log_line("Seeder ativo. Ctrl+C para encerrar.");
            while (running)
                std::this_thread::sleep_for(200ms);
        }
        for (auto& t : workers)
            t.join();
    } catch (...) {
        running = false;
        server_thread.join();
        if (local_fd >= 0)
            close(local_fd);
        throw;
    }

    server_thread.join();
    if (local_fd >= 0)
        close(local_fd);
}

void Peer::init() {
    fs::create_directories(dir);

    if (seed) {
        meta = Metadata::from_file(source_path, meta.block_size);
        std::ofstream out(dir + "/" + meta.name + ".meta", std::ios::binary);
        out << meta.serialize();
        local_path = source_path;
        local_fd = open(source_path.c_str(), O_RDONLY);
        have.assign(meta.block_count(), 1);
        completed = meta.block_count();
        finished = true;
    } else {
        while (running && !fetch_metadata())
            std::this_thread::sleep_for(500ms);
        if (!running)
            return;

        local_path = dir + "/" + meta.name + ".part";
        local_fd = open(local_path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (local_fd < 0 || ftruncate(local_fd, meta.size) < 0)
            throw std::runtime_error("nao criou arquivo parcial");
        have.assign(meta.block_count(), 0);
    }

    if (local_fd < 0)
        throw std::runtime_error("nao abriu arquivo");
    claimed.assign(meta.block_count(), 0);
    ready = true;
    log_line(std::string(seed ? "SEED " : "LEECH ") + meta.name +
             " blocos=" + std::to_string(meta.block_count()) + " endereco=" + self.str());
}

std::vector<uint8_t> Peer::respond(const Request& request) {
    if (!ready)
        return {};

    switch (request.command) {
    case Command::Metadata: {
        std::string text = meta.serialize();
        return {text.begin(), text.end()};
    }
    case Command::BlockMap: {
        std::lock_guard<std::mutex> lock(blocks_mutex);
        return have;
    }
    case Command::GetBlock:
        return read_block(request.block_index);
    }
    return {};
}

std::vector<uint8_t> Peer::read_block(size_t index) {
    if (index >= meta.block_count())
        return {};
    {
        std::lock_guard<std::mutex> lock(blocks_mutex);
        if (!have[index])
            return {};
    }

    std::vector<uint8_t> block(meta.block_length(index));
    off_t offset = static_cast<off_t>(index) * meta.block_size;
    if (pread(local_fd, block.data(), block.size(), offset) != static_cast<ssize_t>(block.size()))
        return {};
    return block;
}

bool Peer::query(const Address& neighbor, const Request& request, std::vector<uint8_t>& body, uint32_t max_size) {
    int fd = connect_to(neighbor);
    if (fd < 0)
        return false;

    std::vector<uint8_t> message = encode_request(request);
    uint32_t length = 0;
    bool ok = send_all(fd, message.data(), message.size()) && recv_u32(fd, length) && length > 0 &&
              length <= max_size;
    if (ok) {
        body.resize(length);
        ok = recv_all(fd, body.data(), body.size());
    }
    close(fd);
    return ok;
}

bool Peer::fetch_metadata() {
    for (const auto& n : neighbors) {
        std::vector<uint8_t> body;
        if (!query(n, {Command::Metadata}, body, MAX_METADATA_SIZE))
            continue;

        try {
            meta = Metadata::parse(std::string(body.begin(), body.end()));
            log_line("META recebida de " + n.str() + " (" + std::to_string(meta.block_count()) + " blocos)");
            return true;
        } catch (const std::exception& e) {
            log_line(std::string("META invalida: ") + e.what());
        }
    }
    return false;
}

void Peer::worker(Address n) {
    const size_t total = meta.block_count();

    while (running && !finished) {
        std::vector<uint8_t> remote;
        if (!fetch_block_map(n, remote)) {
            std::this_thread::sleep_for(300ms);
            continue;
        }

        bool progress = false;
        for (size_t i = 0; i < remote.size() && running && !finished; i++) {
            if (!remote[i])
                continue;
            {
                std::lock_guard<std::mutex> lock(blocks_mutex);
                if (have[i] || claimed[i])
                    continue;
                claimed[i] = 1;
            }

            std::vector<uint8_t> data;
            bool stored = fetch_block(n, i, data) && write_block(i, data);

            std::lock_guard<std::mutex> lock(blocks_mutex);
            if (stored && !have[i]) {
                have[i] = 1;
                size_t done = ++completed;
                progress = true;
                log_line("RECEBIDO bloco=" + std::to_string(i) + " origem=" + n.str() +
                         " progresso=" + std::to_string(done) + "/" + std::to_string(total));
                if (done == total)
                    finished = true;
            }
            claimed[i] = 0;
        }
        if (!progress)
            std::this_thread::sleep_for(200ms);
    }
}

bool Peer::fetch_block_map(const Address& n, std::vector<uint8_t>& remote) {
    uint32_t total = static_cast<uint32_t>(meta.block_count());
    return query(n, {Command::BlockMap}, remote, total) && remote.size() == total;
}

bool Peer::fetch_block(const Address& n, size_t index, std::vector<uint8_t>& data) {
    size_t length = meta.block_length(index);
    return query(n, {Command::GetBlock, static_cast<uint32_t>(index)}, data, static_cast<uint32_t>(length)) &&
           data.size() == length && sha256_hex(data.data(), data.size()) == meta.block_sha256[index];
}

bool Peer::write_block(size_t index, const std::vector<uint8_t>& data) {
    off_t offset = static_cast<off_t>(index) * meta.block_size;
    return pwrite(local_fd, data.data(), data.size(), offset) == static_cast<ssize_t>(data.size());
}

void Peer::finish_download() {
    fsync(local_fd);
    std::string hash = sha256_file(local_path);
    if (hash == meta.sha256) {
        fs::rename(local_path, dir + "/" + meta.name);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        log_line("SUCESSO SHA256=" + hash + " bytes=" + std::to_string(meta.size) + " tempo_ms=" + std::to_string(ms));
    } else {
        log_line("FALHA: SHA256 final diferente");
    }

    log_line("Compartilhando por mais " + std::to_string(linger) + " segundos");
    for (int k = 0; k < linger * 10 && running; k++)
        std::this_thread::sleep_for(100ms);
}
