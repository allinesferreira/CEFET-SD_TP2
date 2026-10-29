#include "peer.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <random>
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
constexpr auto RETRY_INTERVAL = 100ms;

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

std::vector<size_t> shuffled_indices(size_t count) {
    std::vector<size_t> indices(count);
    std::iota(indices.begin(), indices.end(), 0);
    std::shuffle(indices.begin(), indices.end(), std::mt19937(std::random_device{}()));
    return indices;
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
    start = std::chrono::steady_clock::now();
    int listener = listen_on(self);
    log_line("Servidor ouvindo " + self.str());

    std::exception_ptr server_error;
    std::thread server_thread([this, listener, &server_error] {
        try {
            serve(listener, running, [this](const Request& request) { return respond(request); });
        } catch (...) {
            server_error = std::current_exception();
            running = false;
        }
    });

    try {
        init();
        if (running && seed)
            seed_until_stopped();
        else if (running && ready)
            download();
        running = false;
    } catch (...) {
        running = false;
        server_thread.join();
        if (local_fd >= 0)
            close(local_fd);
        if (dir_fd >= 0)
            close(dir_fd);
        throw;
    }

    server_thread.join();
    if (local_fd >= 0)
        close(local_fd);
    if (dir_fd >= 0)
        close(dir_fd);
    if (server_error)
        std::rethrow_exception(server_error);
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
    log_status();

    std::vector<std::thread> workers;
    for (const auto& n : neighbors)
        workers.emplace_back([this, n] { worker(n); });

    auto next_status = std::chrono::steady_clock::now() + STATUS_INTERVAL;
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
    dir_fd = open(dir.c_str(), O_RDONLY | O_DIRECTORY);
    if (dir_fd < 0 || flock(dir_fd, LOCK_EX | LOCK_NB) < 0)
        throw std::runtime_error("pasta " + dir + " ja esta em uso por outro peer");

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
            std::this_thread::sleep_for(RETRY_INTERVAL);
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

Reply Peer::query(const Address& neighbor, const Request& request, std::vector<uint8_t>& body, uint32_t max_size) {
    int fd = connect_to(neighbor);
    if (fd < 0)
        return Reply::Failed;

    std::vector<uint8_t> message = encode_request(request);
    uint32_t length = 0;
    Reply reply = Reply::Failed;
    if (send_all(fd, message.data(), message.size()) && recv_u32(fd, length)) {
        if (length == 0) {
            reply = Reply::Refused;
        } else if (length <= max_size) {
            body.resize(length);
            if (recv_all(fd, body.data(), body.size()))
                reply = Reply::Ok;
        }
    }
    close(fd);
    return reply;
}

bool Peer::fetch_metadata() {
    for (const auto& n : neighbors) {
        std::vector<uint8_t> body;
        if (query(n, {Command::Metadata, self}, body, MAX_METADATA_SIZE) != Reply::Ok)
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
    const std::vector<size_t> order = shuffled_indices(meta.block_count());
    bool reachable = true;

    while (running && !finished) {
        std::vector<uint8_t> remote;
        Reply reply = fetch_block_map(n, remote);
        if ((reply != Reply::Failed) != reachable) {
            reachable = !reachable;
            log_line(std::string(reachable ? "VIZINHO acessivel " : "VIZINHO inacessivel ") + n.str());
        }
        if (reply != Reply::Ok) {
            std::this_thread::sleep_for(RETRY_INTERVAL);
            continue;
        }

        if (!download_available(n, remote, order))
            std::this_thread::sleep_for(RETRY_INTERVAL);
    }
}

bool Peer::download_available(const Address& n, const std::vector<uint8_t>& remote, const std::vector<size_t>& order) {
    bool progress = false;
    for (size_t i : order) {
        if (!running || finished)
            break;
        if (!remote[i] || !claim(i))
            continue;

        std::vector<uint8_t> data;
        Reply reply = fetch_block(n, i, data);
        bool stored = reply == Reply::Ok && write_block(i, data);
        release(i, stored, n);
        progress = progress || stored;
        if (reply == Reply::Failed)
            break;
    }
    return progress;
}

bool Peer::claim(size_t index) {
    std::lock_guard<std::mutex> lock(blocks_mutex);
    if (have[index] || claimed[index])
        return false;
    claimed[index] = 1;
    return true;
}

void Peer::release(size_t index, bool stored, const Address& origin) {
    std::lock_guard<std::mutex> lock(blocks_mutex);
    claimed[index] = 0;
    if (!stored)
        return;

    have[index] = 1;
    received_from[origin.str()]++;
    size_t done = ++completed;
    size_t total = meta.block_count();
    log_line("RECEBIDO bloco=" + std::to_string(index) + " origem=" + origin.str() +
             " progresso=" + std::to_string(done) + "/" + std::to_string(total));
    if (done == total) {
        last_block_time = std::chrono::steady_clock::now();
        finished = true;
    }
}

Reply Peer::fetch_block_map(const Address& n, std::vector<uint8_t>& remote) {
    uint32_t total = static_cast<uint32_t>(meta.block_count());
    Reply reply = query(n, {Command::BlockMap, self}, remote, total);
    if (reply == Reply::Ok && remote.size() != total)
        return Reply::Refused;
    return reply;
}

Reply Peer::fetch_block(const Address& n, size_t index, std::vector<uint8_t>& data) {
    auto length = static_cast<uint32_t>(meta.block_length(index));
    Reply reply = query(n, {Command::GetBlock, self, static_cast<uint32_t>(index)}, data, length);
    if (reply == Reply::Ok && sha256_hex(data.data(), data.size()) != meta.block_sha256[index]) {
        log_line("INVALIDO bloco=" + std::to_string(index) + " origem=" + n.str());
        return Reply::Refused;
    }
    return reply;
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
    if (hash != meta.sha256) {
        log_line("FALHA: SHA256 final diferente");
        throw std::runtime_error("arquivo remontado nao confere com os metadados");
    }

    fs::rename(local_path, dir + "/" + meta.name);
    save_metadata();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(last_block_time - start).count();
    log_line("SUCESSO SHA256=" + hash + " bytes=" + std::to_string(meta.size) + " tempo_ms=" + std::to_string(ms));

    log_line("Compartilhando por mais " + std::to_string(linger) + " segundos");
    for (int k = 0; k < linger * 10 && running; k++)
        std::this_thread::sleep_for(100ms);
}
