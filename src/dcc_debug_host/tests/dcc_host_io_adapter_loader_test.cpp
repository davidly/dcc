#include "dcc_host_io_adapter_loader.hpp"

#include <array>
#include <cassert>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

int main(int argc, char **argv)
{
    assert(argc == 7);
    dcc_debug_io_adapter_config_t config{};
    config.abi_version = DCC_DEBUG_IO_ADAPTER_ABI_VERSION;
    config.struct_size = sizeof(config);
    config.host.abi_version = DCC_DEBUG_IO_ADAPTER_ABI_VERSION;
    config.host.struct_size = sizeof(config.host);

    IoAdapterLoader loader;
    std::string error;
    assert(!loader.load(fs::path(argv[1]).concat(".missing"), config, error));
    assert(error.find("cannot load I/O adapter") != std::string::npos);
    assert(!loader.loaded());

    error.clear();
    assert(!loader.load(argv[2], config, error));
    assert(error.find("does not export") != std::string::npos);
    assert(!loader.loaded());

    error.clear();
    assert(!loader.load(argv[3], config, error));
    assert(error.find("fixture init failure") != std::string::npos);
    assert(!loader.loaded());

    error.clear();
    assert(!loader.load(argv[4], config, error));
    assert(error.find("invalid ABI descriptor") != std::string::npos);
    assert(!loader.loaded());

    error.clear();
    assert(!loader.load(argv[5], config, error));
    assert(error.find("invalid ABI descriptor") != std::string::npos);
    assert(!loader.loaded());

    error.clear();
    assert(loader.load(argv[1], config, error));
    assert(loader.loaded());
    assert(loader.input(0x12) == (0x12 ^ 0x5a));
    loader.output(0x34, 0x56);
    std::array<uint8_t, 2> input{{'A', 'B'}};
    std::array<uint8_t, 2> output{};
    assert(loader.terminal_input(input.data(), input.size(), output.data(), output.size(), 1) == 2);
    assert(output == input);
    assert(loader.terminal_poll(output.data(), output.size(), 2) == 0);
    loader.close();
    assert(!loader.loaded());
    assert(loader.input(0x12) == 0);

    error.clear();
    assert(loader.load(argv[6], config, error));
    assert(loader.terminal_input(input.data(), input.size(), output.data(), output.size(), 3) == output.size());
    assert(output[0] == 0xa5);
    assert(loader.terminal_poll(output.data(), output.size(), 4) == output.size());
    assert(output[0] == 0x5a);
    return 0;
}