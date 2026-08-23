#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

class TargetTerminal
{
public:
    TargetTerminal() = default;
    ~TargetTerminal();

    TargetTerminal(const TargetTerminal &) = delete;
    TargetTerminal &operator=(const TargetTerminal &) = delete;

    bool connect(const std::filesystem::path &endpoint_file,
                 std::function<void(const uint8_t *, size_t)> input,
                 std::string &error,
                 unsigned endpoint_attempts = 100,
                 unsigned retry_delay_ms = 50);
    void stop();
    bool write(uint8_t character);
    bool connected() const { return connected_; }

private:
    void read_loop();
    void close_socket();

    std::function<void(const uint8_t *, size_t)> input_;
    std::thread reader_;
    mutable std::mutex socket_mutex_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::atomic<std::intptr_t> socket_{-1};
#ifdef _WIN32
    bool winsock_started_ = false;
#endif
};