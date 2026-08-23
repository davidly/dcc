#pragma once

#include "dcc_debug_io_adapter.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

class IoAdapterLoader
{
public:
    IoAdapterLoader() = default;
    ~IoAdapterLoader();

    IoAdapterLoader(const IoAdapterLoader &) = delete;
    IoAdapterLoader &operator=(const IoAdapterLoader &) = delete;

    bool load(const std::filesystem::path &library,
              const dcc_debug_io_adapter_config_t &config,
              std::string &error);
    void close();
    uint8_t input(uint8_t port) const;
    void output(uint8_t port, uint8_t data) const;
    size_t terminal_input(const uint8_t *input, size_t input_size,
                          uint8_t *output, size_t output_size,
                          uint64_t now_ms) const;
    size_t terminal_poll(uint8_t *output, size_t output_size,
                         uint64_t now_ms) const;
    bool loaded() const { return library_ != nullptr; }

private:
    void *library_ = nullptr;
    dcc_debug_io_adapter_t adapter_{};
};