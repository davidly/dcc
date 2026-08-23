#include "dcc_host_target_terminal.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using TestSocket = SOCKET;
using TestSocketLength = int;
constexpr TestSocket kInvalidTestSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using TestSocket = int;
using TestSocketLength = socklen_t;
constexpr TestSocket kInvalidTestSocket = -1;
#endif

namespace fs = std::filesystem;

void close_test_socket(TestSocket socket)
{
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

unsigned reserve_non_listening_port(TestSocket &socket)
{
#ifdef _WIN32
    WSADATA data{};
    assert(WSAStartup(MAKEWORD(2, 2), &data) == 0);
#endif
    socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(socket != kInvalidTestSocket);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = 0;
    assert(inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) == 1);
    assert(::bind(socket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
    TestSocketLength size = sizeof(address);
    assert(getsockname(socket, reinterpret_cast<sockaddr *>(&address), &size) == 0);
    return ntohs(address.sin_port);
}

int main()
{
    auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    fs::path root = fs::temp_directory_path() /
                    ("dcc-debug-terminal-test-" + std::to_string(stamp));
    fs::create_directories(root);
    fs::path endpoint = root / "terminal.endpoint";
    TargetTerminal terminal;
    std::string error;

    std::ofstream(endpoint) << "not an endpoint\n";
    assert(!terminal.connect(endpoint, [](const uint8_t *, size_t) {}, error, 1, 0));
    assert(error.find("cannot read target terminal endpoint") != std::string::npos);
    assert(!terminal.connected() && !terminal.write('X'));

    TestSocket reserved = kInvalidTestSocket;
    unsigned port = reserve_non_listening_port(reserved);
    close_test_socket(reserved);
    std::ofstream(endpoint, std::ios::trunc) << "127.0.0.1 " << port << " token\n";
    error.clear();
    assert(!terminal.connect(endpoint, [](const uint8_t *, size_t) {}, error, 1, 0));
    assert(error.find("cannot connect to the VS Code target terminal") != std::string::npos);
    assert(!terminal.connected() && !terminal.write('X'));
    terminal.stop();
#ifdef _WIN32
    WSACleanup();
#endif
    fs::remove_all(root);
    return 0;
}