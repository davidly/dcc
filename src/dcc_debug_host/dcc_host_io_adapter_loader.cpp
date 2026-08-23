#include "dcc_host_io_adapter_loader.hpp"

#include <array>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace
{
#ifdef _WIN32
std::string windows_error(DWORD code)
{
    char *message = nullptr;
    DWORD size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                    FORMAT_MESSAGE_FROM_SYSTEM |
                                    FORMAT_MESSAGE_IGNORE_INSERTS,
                                nullptr, code, 0,
                                reinterpret_cast<char *>(&message), 0, nullptr);
    std::string result = size && message ? std::string(message, size)
                                         : "Windows error " + std::to_string(code);
    if (message)
        LocalFree(message);
    while (!result.empty() && (result.back() == '\r' || result.back() == '\n'))
        result.pop_back();
    return result;
}
#endif
}

IoAdapterLoader::~IoAdapterLoader()
{
    close();
}

bool IoAdapterLoader::load(const std::filesystem::path &library,
                           const dcc_debug_io_adapter_config_t &config,
                           std::string &error)
{
    close();
    std::array<char, 512> adapter_error{};

#ifdef _WIN32
    HMODULE handle = LoadLibraryW(library.c_str());
    if (!handle)
    {
        error = "cannot load I/O adapter " + library.string() + ": " +
                windows_error(GetLastError());
        return false;
    }
    library_ = handle;
    auto initialize = reinterpret_cast<dcc_debug_io_adapter_init_fn>(
        GetProcAddress(handle, DCC_DEBUG_IO_ADAPTER_INIT_SYMBOL));
#else
    void *handle = dlopen(library.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle)
    {
        const char *message = dlerror();
        error = "cannot load I/O adapter " + library.string() + ": " +
                (message ? message : "unknown dynamic loader error");
        return false;
    }
    library_ = handle;
    dlerror();
    auto initialize = reinterpret_cast<dcc_debug_io_adapter_init_fn>(
        dlsym(handle, DCC_DEBUG_IO_ADAPTER_INIT_SYMBOL));
#endif

    if (!initialize)
    {
        error = "I/O adapter does not export "
                DCC_DEBUG_IO_ADAPTER_INIT_SYMBOL ": " + library.string();
        close();
        return false;
    }
    if (!initialize(&config, &adapter_, adapter_error.data(), adapter_error.size()))
    {
        error = "cannot initialize I/O adapter " + library.string();
        if (adapter_error[0] != '\0')
            error += ": " + std::string(adapter_error.data());
        close();
        return false;
    }
    if (adapter_.abi_version != DCC_DEBUG_IO_ADAPTER_ABI_VERSION ||
        adapter_.struct_size < sizeof(adapter_) || !adapter_.input ||
        !adapter_.output || !adapter_.close)
    {
        error = "I/O adapter returned an invalid ABI descriptor: " + library.string();
        close();
        return false;
    }
    return true;
}

void IoAdapterLoader::close()
{
    if (adapter_.close)
        adapter_.close(adapter_.context);
    adapter_ = {};
    if (!library_)
        return;
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(library_));
#else
    dlclose(library_);
#endif
    library_ = nullptr;
}

uint8_t IoAdapterLoader::input(uint8_t port) const
{
    return adapter_.input ? adapter_.input(adapter_.context, port) : 0;
}

void IoAdapterLoader::output(uint8_t port, uint8_t data) const
{
    if (adapter_.output)
        adapter_.output(adapter_.context, port, data);
}

size_t IoAdapterLoader::terminal_input(
    const uint8_t *input, size_t input_size,
    uint8_t *output, size_t output_size, uint64_t now_ms) const
{
    if (adapter_.terminal_input)
    {
        size_t count = adapter_.terminal_input(adapter_.context, input, input_size,
                                               output, output_size, now_ms);
        return count < output_size ? count : output_size;
    }
    size_t count = input_size < output_size ? input_size : output_size;
    if (count != 0)
        std::memcpy(output, input, count);
    return count;
}

size_t IoAdapterLoader::terminal_poll(
    uint8_t *output, size_t output_size, uint64_t now_ms) const
{
    if (!adapter_.terminal_poll)
        return 0;
    size_t count = adapter_.terminal_poll(
        adapter_.context, output, output_size, now_ms);
    return count < output_size ? count : output_size;
}