#include "dcc_host_target_terminal.hpp"

#include <chrono>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace
{
#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;

void close_native_socket(NativeSocket socket)
{
    closesocket(socket);
}
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;

void close_native_socket(NativeSocket socket)
{
    close(socket);
}
#endif

NativeSocket native_socket(std::intptr_t socket)
{
    return static_cast<NativeSocket>(socket);
}

int send_flags()
{
#ifdef MSG_NOSIGNAL
    return MSG_NOSIGNAL;
#else
    return 0;
#endif
}

bool send_all(NativeSocket socket, const char *data, size_t size)
{
    size_t sent_total = 0;
    while (sent_total < size)
    {
        int sent = ::send(socket, data + sent_total,
                          static_cast<int>(size - sent_total), send_flags());
        if (sent <= 0)
            return false;
        sent_total += static_cast<size_t>(sent);
    }
    return true;
}
}

TargetTerminal::~TargetTerminal()
{
    stop();
}

bool TargetTerminal::connect(const std::filesystem::path &endpoint_file,
                             std::function<void(const uint8_t *, size_t)> input,
                             std::string &error,
                             unsigned endpoint_attempts,
                             unsigned retry_delay_ms)
{
    std::string host;
    std::string token;
    unsigned port = 0;

#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
    {
        error = "cannot initialize Winsock for the target terminal";
        return false;
    }
    winsock_started_ = true;
#endif

    for (unsigned attempt = 0; attempt < endpoint_attempts; ++attempt)
    {
        std::ifstream endpoint(endpoint_file);
        if (endpoint >> host >> port >> token && token.size() <= 128)
            break;
        host.clear();
        port = 0;
        token.clear();
        std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms));
    }
    if (host != "127.0.0.1" || port == 0 || port > 65535 || token.empty())
    {
        error = "cannot read target terminal endpoint: " + endpoint_file.string();
        stop();
        return false;
    }

    NativeSocket socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == kInvalidSocket)
    {
        error = "cannot create target terminal socket";
        stop();
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1 ||
        ::connect(socket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0)
    {
        close_native_socket(socket);
        error = "cannot connect to the VS Code target terminal";
        stop();
        return false;
    }

#if defined(__APPLE__) && defined(SO_NOSIGPIPE)
    int enabled = 1;
    setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif

    std::string authentication = token + "\n";
    if (!send_all(socket, authentication.data(), authentication.size()))
    {
        close_native_socket(socket);
        error = "cannot authenticate the VS Code target terminal";
        stop();
        return false;
    }

    input_ = std::move(input);
    socket_.store(static_cast<std::intptr_t>(socket));
    running_ = true;
    connected_ = true;
    reader_ = std::thread(&TargetTerminal::read_loop, this);
    return true;
}

void TargetTerminal::read_loop()
{
    uint8_t buffer[256];
    while (running_)
    {
        NativeSocket socket = native_socket(socket_.load());
        if (socket == kInvalidSocket)
            break;
        int received = ::recv(socket, reinterpret_cast<char *>(buffer), sizeof(buffer), 0);
        if (received <= 0)
            break;
        if (input_)
            input_(buffer, static_cast<size_t>(received));
    }
    connected_ = false;
    running_ = false;
    close_socket();
}

void TargetTerminal::close_socket()
{
    std::lock_guard<std::mutex> lock(socket_mutex_);
    NativeSocket socket = native_socket(socket_.exchange(-1));
    if (socket == kInvalidSocket)
        return;
#ifdef _WIN32
    shutdown(socket, SD_BOTH);
#else
    shutdown(socket, SHUT_RDWR);
#endif
    close_native_socket(socket);
}

void TargetTerminal::stop()
{
    running_ = false;
    connected_ = false;
    {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        NativeSocket socket = native_socket(socket_.load());
        if (socket != kInvalidSocket)
        {
#ifdef _WIN32
            shutdown(socket, SD_BOTH);
#else
            shutdown(socket, SHUT_RDWR);
#endif
        }
    }
    if (reader_.joinable() && reader_.get_id() != std::this_thread::get_id())
        reader_.join();
    close_socket();
#ifdef _WIN32
    if (winsock_started_)
    {
        WSACleanup();
        winsock_started_ = false;
    }
#endif
}

bool TargetTerminal::write(uint8_t character)
{
    bool failed = false;
    {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        NativeSocket socket = native_socket(socket_.load());
        if (!connected_ || socket == kInvalidSocket)
            return false;
        int sent = ::send(socket, reinterpret_cast<const char *>(&character), 1, send_flags());
        if (sent == 1)
            return true;
        connected_ = false;
        running_ = false;
        failed = true;
    }
    if (failed)
    {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        NativeSocket socket = native_socket(socket_.load());
        if (socket != kInvalidSocket)
        {
#ifdef _WIN32
            shutdown(socket, SD_BOTH);
#else
            shutdown(socket, SHUT_RDWR);
#endif
        }
    }
    return false;
}