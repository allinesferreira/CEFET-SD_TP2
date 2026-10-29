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
constexpr auto STATUS_INTERVAL = 1s;

std::string describe(const Request& request) {
    switch (request.command) {
    case Command::Metadata:
        return "metadados";
    case Command::BlockMap:
        return "mapa";
    case Command::GetBlock:
        return "bloco=" + std::to_string(request.block_index);
    }
    return "?";
}

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
        if (seed)
            seed_until_stopped();
        else if (ready)
            download();
        running = false;
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
    if (ready)
        log_summary();
}

void Peer::seed_until_stopped() {
    log_status();
    log_line("Seeder ativo. Ctrl+C para encerrar.");
    while (running)
        std::this_thread::sleep_for(200ms);
}

void Peer::download() {
    start = std::chrono::steady_clock::now();
    log_status();

    std::vector<std::thread> workers;
    for (const auto& n : neighbors)
        workers.emplace_back([this, n] { worker(n); });

    auto next_status = start + STATUS_INTERVAL;
    while (running && !finished) {
        std::this_thread::sleep_for(100ms);
        if (std::chrono::steady_clock::now() >= next_status) {
            log_status();
            next_status += STATUS_INTERVAL;
        }
    }
    for (auto& t : workers)
        t.join();

    if (finished) {
        log_status();
        finish_download();
    }
}

void Peer::log_status() {
    size_t done = completed;
    size_t total = meta.block_count();
    log_line("STATUS blocos=" + std::to_string(done) + "/" + std::to_string(total) + " (" +
             std::to_string(done * 100 / total) + "%)");
}

void Peer::log_summary() {
    size_t received = 0;
    for (const auto& [origin, count] : received_from) {
        log_line("RESUMO origem=" + origin + " blocos=" + std::to_string(count));
        received += count;
    }
    size_t sent = 0;
    for (const auto& [destination, count] : sent_to) {
        log_line("RESUMO destino=" + destination + " blocos=" + std::to_string(count));
        sent += count;
    }
    log_line("RESUMO recebidos=" + std::to_string(received) + " enviados=" + std::to_string(sent));
}

void Peer::init() {
    fs::create_directories(dir);

    if (seed) {
        meta = Metadata::from_file(source_path, meta.block_size);
        save_metadata();
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
    std::vector<uint8_t> body = build_response(request);
    if (request.command == Command::GetBlock && !body.empty())
        sent_to[request.sender.str()]++;
    log_line(std::string(body.empty() ? "NEGADO " : "ENVIADO ") + describe(request) +
             " destino=" + request.sender.str());
    return body;
}

std::vector<uint8_t> Peer::build_response(const Request& request) {
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
        if (!query(n, {Command::Metadata, self}, body, MAX_METADATA_SIZE))
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
                received_from[n.str()]++;
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
    return query(n, {Command::BlockMap, self}, remote, total) && remote.size() == total;
}

bool Peer::fetch_block(const Address& n, size_t index, std::vector<uint8_t>& data) {
    size_t length = meta.block_length(index);
    return query(n, {Command::GetBlock, self, static_cast<uint32_t>(index)}, data, static_cast<uint32_t>(length)) &&
           data.size() == length && sha256_hex(data.data(), data.size()) == meta.block_sha256[index];
}

bool Peer::write_block(size_t index, const std::vector<uint8_t>& data) {
    off_t offset = static_cast<off_t>(index) * meta.block_size;
    return pwrite(local_fd, data.data(), data.size(), offset) == static_cast<ssize_t>(data.size());
}

void Peer::save_metadata() {
    std::ofstream out(dir + "/" + meta.name + ".meta", std::ios::binary);
    out << meta.serialize();
}

void Peer::finish_download() {
    fsync(local_fd);
    std::string hash = sha256_file(local_path);
    if (hash == meta.sha256) {
        fs::rename(local_path, dir + "/" + meta.name);
        save_metadata();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        log_line("SUCESSO SHA256=" + hash + " bytes=" + std::to_string(meta.size) + " tempo_ms=" + std::to_string(ms));
    } else {
        log_line("FALHA: SHA256 final diferente");
    }

    log_line("Compartilhando por mais " + std::to_string(linger) + " segundos");
    for (int k = 0; k < linger * 10 && running; k++)
        std::this_thread::sleep_for(100ms);
}
